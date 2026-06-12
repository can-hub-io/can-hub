//! `web.db` store: users, groups, group permissions, memberships and
//! server-side sessions. Synchronous (rusqlite); the HTTP layer calls it on a
//! blocking task behind a mutex.

use std::collections::HashSet;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use rand::Rng;
use rusqlite::{params, Connection, OptionalExtension};
use sha2::{Digest, Sha256};

use super::password::{hash_password, verify_password};
use super::Permission;

/// Session expiry: a session dies after this much inactivity, or this long
/// after creation regardless of activity.
const SESSION_IDLE_SECS: i64 = 12 * 3600;
const SESSION_ABSOLUTE_SECS: i64 = 7 * 24 * 3600;

/// Shortest password the store will accept. The UI validates too, but the
/// store is the real boundary.
pub const MIN_PASSWORD_LEN: usize = 8;

/// Most recent audit rows kept by `purge_expired`; older ones are trimmed so
/// the log cannot grow without bound.
const AUDIT_RETAIN: i64 = 10_000;

#[derive(Debug)]
pub enum StoreError {
    Db(rusqlite::Error),
    Hash(String),
    /// A unique constraint or precondition was violated (name taken, etc.).
    Conflict(&'static str),
}

impl From<rusqlite::Error> for StoreError {
    fn from(error: rusqlite::Error) -> Self {
        StoreError::Db(error)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UserSummary {
    pub id: i64,
    pub name: String,
    pub enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Group {
    pub id: i64,
    pub name: String,
    pub permissions: Vec<String>,
}

/// Tokens minted with a session: the opaque session token (cookie) and the
/// CSRF token the client echoes in a header on mutating requests.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SessionTokens {
    pub session_token: String,
    pub csrf_token: String,
}

/// A validated session's identity and CSRF token.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SessionInfo {
    pub user_id: i64,
    pub csrf_token: String,
}

/// A user's stored credential, fetched cheaply under the store lock so the
/// expensive argon2 verification can run off it (off the lock).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Credential {
    pub user_id: i64,
    pub password_hash: String,
    pub enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AuditEntry {
    pub at: i64,
    pub actor: String,
    pub action: String,
    pub target: String,
    pub status: u16,
}

pub struct AuthStore {
    connection: Connection,
}

impl AuthStore {
    pub fn open(path: impl AsRef<Path>) -> Result<Self, StoreError> {
        let store = AuthStore { connection: Connection::open(path)? };
        store.create_schema()?;
        Ok(store)
    }

    pub fn open_in_memory() -> Result<Self, StoreError> {
        let store = AuthStore { connection: Connection::open_in_memory()? };
        store.create_schema()?;
        Ok(store)
    }

    fn create_schema(&self) -> Result<(), StoreError> {
        self.connection.execute_batch(
            "PRAGMA foreign_keys = ON;
             CREATE TABLE IF NOT EXISTS users (
                 id INTEGER PRIMARY KEY,
                 name TEXT NOT NULL UNIQUE,
                 password_hash TEXT NOT NULL,
                 enabled INTEGER NOT NULL DEFAULT 1,
                 created_at INTEGER NOT NULL
             );
             CREATE TABLE IF NOT EXISTS groups (
                 id INTEGER PRIMARY KEY,
                 name TEXT NOT NULL UNIQUE
             );
             CREATE TABLE IF NOT EXISTS group_permissions (
                 group_id INTEGER NOT NULL REFERENCES groups(id) ON DELETE CASCADE,
                 permission TEXT NOT NULL,
                 PRIMARY KEY (group_id, permission)
             );
             CREATE TABLE IF NOT EXISTS user_groups (
                 user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                 group_id INTEGER NOT NULL REFERENCES groups(id) ON DELETE CASCADE,
                 PRIMARY KEY (user_id, group_id)
             );
             CREATE TABLE IF NOT EXISTS sessions (
                 token TEXT PRIMARY KEY,
                 user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                 created_at INTEGER NOT NULL,
                 last_seen_at INTEGER NOT NULL,
                 csrf_token TEXT NOT NULL
             );
             CREATE TABLE IF NOT EXISTS audit_log (
                 id INTEGER PRIMARY KEY,
                 at INTEGER NOT NULL,
                 actor_id INTEGER,
                 actor TEXT NOT NULL,
                 action TEXT NOT NULL,
                 target TEXT NOT NULL,
                 status INTEGER NOT NULL
             );",
        )?;
        Ok(())
    }

    // ----------------------------------------------------------- users

    pub fn user_count(&self) -> Result<u64, StoreError> {
        let count: i64 = self.connection.query_row("SELECT COUNT(*) FROM users", [], |row| row.get(0))?;
        Ok(count as u64)
    }

    pub fn create_user(&self, name: &str, password: &str, enabled: bool) -> Result<i64, StoreError> {
        insert_user(&self.connection, name, password, enabled)
    }

    /// Provision an admin atomically: create the user, ensure the `admins`
    /// group holds every permission, and add the user to it — all in one
    /// transaction so a mid-sequence failure leaves no half-built admin. When
    /// `require_first` is set the call also asserts (inside the transaction)
    /// that no users exist yet, for first-run bootstrap.
    pub fn bootstrap_admin(&self, name: &str, password: &str, require_first: bool) -> Result<i64, StoreError> {
        let transaction = self.connection.unchecked_transaction()?;
        if require_first {
            let count: i64 = transaction.query_row("SELECT COUNT(*) FROM users", [], |row| row.get(0))?;
            if count != 0 {
                return Err(StoreError::Conflict("setup already completed"));
            }
        }
        let user_id = insert_user(&transaction, name, password, true)?;
        let group_id = ensure_group_id(&transaction, "admins")?;
        transaction.execute("DELETE FROM group_permissions WHERE group_id = ?1", params![group_id])?;
        for permission in Permission::ALL {
            transaction.execute(
                "INSERT INTO group_permissions (group_id, permission) VALUES (?1, ?2)",
                params![group_id, permission.as_str()],
            )?;
        }
        transaction.execute(
            "INSERT OR IGNORE INTO user_groups (user_id, group_id) VALUES (?1, ?2)",
            params![user_id, group_id],
        )?;
        transaction.commit()?;
        Ok(user_id)
    }

    /// Verify credentials. Returns the user id on success; an unknown user, a
    /// bad password or a disabled account all yield None (no oracle).
    pub fn verify_login(&self, name: &str, password: &str) -> Result<Option<i64>, StoreError> {
        let row: Option<(i64, String, i64)> = self
            .connection
            .query_row(
                "SELECT id, password_hash, enabled FROM users WHERE name = ?1",
                params![name],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
            )
            .optional()?;
        match row {
            Some((id, hash, enabled)) if enabled != 0 && verify_password(password, &hash) => Ok(Some(id)),
            _ => Ok(None),
        }
    }

    /// Replace a user's password. Invalidates the user's sessions except an
    /// optional one to keep (a user changing their own password stays logged in
    /// on the current session; an admin resetting someone else's passes None,
    /// forcing that user to re-authenticate everywhere).
    pub fn update_password(
        &self,
        user_id: i64,
        password: &str,
        keep_session_token: Option<&str>,
    ) -> Result<(), StoreError> {
        if password.len() < MIN_PASSWORD_LEN {
            return Err(StoreError::Conflict("password too short"));
        }
        let hash = hash_password(password).map_err(StoreError::Hash)?;
        let transaction = self.connection.unchecked_transaction()?;
        let updated =
            transaction.execute("UPDATE users SET password_hash = ?1 WHERE id = ?2", params![hash, user_id])?;
        if updated == 0 {
            return Err(StoreError::Conflict("no such user"));
        }
        match keep_session_token {
            Some(token) => transaction.execute(
                "DELETE FROM sessions WHERE user_id = ?1 AND token != ?2",
                params![user_id, hash_token(token)],
            )?,
            None => transaction.execute("DELETE FROM sessions WHERE user_id = ?1", params![user_id])?,
        };
        transaction.commit()?;
        Ok(())
    }

    /// Whether `password` matches the stored hash for `user_id` (for confirming
    /// the current password on a self-service change).
    pub fn verify_user_password(&self, user_id: i64, password: &str) -> Result<bool, StoreError> {
        let hash: Option<String> = self
            .connection
            .query_row("SELECT password_hash FROM users WHERE id = ?1", params![user_id], |row| row.get(0))
            .optional()?;
        Ok(hash.is_some_and(|hash| verify_password(password, &hash)))
    }

    /// Fetch a user's credential by name (the cheap, lock-held half of login;
    /// the caller runs argon2 verification off the lock).
    pub fn credential(&self, name: &str) -> Result<Option<Credential>, StoreError> {
        Ok(self
            .connection
            .query_row(
                "SELECT id, password_hash, enabled FROM users WHERE name = ?1",
                params![name],
                |row| {
                    Ok(Credential {
                        user_id: row.get(0)?,
                        password_hash: row.get(1)?,
                        enabled: row.get::<_, i64>(2)? != 0,
                    })
                },
            )
            .optional()?)
    }

    pub fn user_name(&self, user_id: i64) -> Result<Option<String>, StoreError> {
        Ok(self
            .connection
            .query_row("SELECT name FROM users WHERE id = ?1", params![user_id], |row| row.get(0))
            .optional()?)
    }

    pub fn list_users(&self) -> Result<Vec<UserSummary>, StoreError> {
        let mut statement = self.connection.prepare("SELECT id, name, enabled FROM users ORDER BY name")?;
        let rows = statement.query_map([], |row| {
            Ok(UserSummary { id: row.get(0)?, name: row.get(1)?, enabled: row.get::<_, i64>(2)? != 0 })
        })?;
        Ok(rows.collect::<Result<_, _>>()?)
    }

    /// Enable or disable a user. Disabling refuses to target the requesting
    /// user or to remove the last enabled holder of `users.manage` (which would
    /// lock everyone out of administration).
    pub fn set_user_enabled(&self, actor_id: i64, user_id: i64, enabled: bool) -> Result<(), StoreError> {
        if !enabled && actor_id == user_id {
            return Err(StoreError::Conflict("cannot disable your own account"));
        }
        let transaction = self.connection.unchecked_transaction()?;
        let before = users_manage_holder_count(&transaction)?;
        transaction.execute("UPDATE users SET enabled = ?1 WHERE id = ?2", params![enabled as i64, user_id])?;
        if !enabled && before > 0 && users_manage_holder_count(&transaction)? == 0 {
            return Err(StoreError::Conflict("would disable the last users.manage account"));
        }
        transaction.commit()?;
        Ok(())
    }

    /// Delete a user. Refuses to target the requesting user or to remove the
    /// last enabled holder of `users.manage`.
    pub fn delete_user(&self, actor_id: i64, user_id: i64) -> Result<(), StoreError> {
        if actor_id == user_id {
            return Err(StoreError::Conflict("cannot delete your own account"));
        }
        let transaction = self.connection.unchecked_transaction()?;
        let before = users_manage_holder_count(&transaction)?;
        transaction.execute("DELETE FROM users WHERE id = ?1", params![user_id])?;
        if before > 0 && users_manage_holder_count(&transaction)? == 0 {
            return Err(StoreError::Conflict("would remove the last users.manage account"));
        }
        transaction.commit()?;
        Ok(())
    }

    // ----------------------------------------------------------- groups

    pub fn create_group(&self, name: &str) -> Result<i64, StoreError> {
        self.connection
            .execute("INSERT INTO groups (name) VALUES (?1)", params![name])
            .map_err(|error| match error {
                rusqlite::Error::SqliteFailure(e, _) if e.code == rusqlite::ErrorCode::ConstraintViolation => {
                    StoreError::Conflict("group name already exists")
                }
                other => StoreError::Db(other),
            })?;
        Ok(self.connection.last_insert_rowid())
    }

    /// Delete a group. Refuses the change if it would leave zero enabled users
    /// holding `users.manage`.
    pub fn delete_group(&self, group_id: i64) -> Result<(), StoreError> {
        let transaction = self.connection.unchecked_transaction()?;
        let before = users_manage_holder_count(&transaction)?;
        transaction.execute("DELETE FROM groups WHERE id = ?1", params![group_id])?;
        if before > 0 && users_manage_holder_count(&transaction)? == 0 {
            return Err(StoreError::Conflict("would remove the last users.manage account"));
        }
        transaction.commit()?;
        Ok(())
    }

    /// Replace a group's permission set in one transaction. Refuses the change
    /// if it would leave zero enabled users holding `users.manage`.
    pub fn set_group_permissions(&self, group_id: i64, permissions: &[Permission]) -> Result<(), StoreError> {
        let transaction = self.connection.unchecked_transaction()?;
        let before = users_manage_holder_count(&transaction)?;
        transaction.execute("DELETE FROM group_permissions WHERE group_id = ?1", params![group_id])?;
        for permission in permissions {
            transaction.execute(
                "INSERT INTO group_permissions (group_id, permission) VALUES (?1, ?2)",
                params![group_id, permission.as_str()],
            )?;
        }
        if before > 0 && users_manage_holder_count(&transaction)? == 0 {
            return Err(StoreError::Conflict("would remove the last users.manage account"));
        }
        transaction.commit()?;
        Ok(())
    }

    pub fn list_groups(&self) -> Result<Vec<Group>, StoreError> {
        let mut statement = self.connection.prepare(
            "SELECT g.id, g.name, gp.permission
             FROM groups g
             LEFT JOIN group_permissions gp ON gp.group_id = g.id
             ORDER BY g.name, gp.permission",
        )?;
        let rows = statement.query_map([], |row| {
            Ok((row.get::<_, i64>(0)?, row.get::<_, String>(1)?, row.get::<_, Option<String>>(2)?))
        })?;
        let mut groups: Vec<Group> = Vec::new();
        for row in rows {
            let (id, name, permission) = row?;
            if groups.last().map(|group| group.id) != Some(id) {
                groups.push(Group { id, name, permissions: Vec::new() });
            }
            if let Some(permission) = permission {
                groups.last_mut().expect("just pushed").permissions.push(permission);
            }
        }
        Ok(groups)
    }

    /// Group ids a user belongs to (for the management UI).
    pub fn user_group_ids(&self, user_id: i64) -> Result<Vec<i64>, StoreError> {
        let mut statement =
            self.connection.prepare("SELECT group_id FROM user_groups WHERE user_id = ?1 ORDER BY group_id")?;
        let rows = statement.query_map(params![user_id], |row| row.get(0))?;
        Ok(rows.collect::<Result<_, _>>()?)
    }

    pub fn add_user_to_group(&self, user_id: i64, group_id: i64) -> Result<(), StoreError> {
        self.connection.execute(
            "INSERT OR IGNORE INTO user_groups (user_id, group_id) VALUES (?1, ?2)",
            params![user_id, group_id],
        )?;
        Ok(())
    }

    pub fn remove_user_from_group(&self, user_id: i64, group_id: i64) -> Result<(), StoreError> {
        self.connection.execute(
            "DELETE FROM user_groups WHERE user_id = ?1 AND group_id = ?2",
            params![user_id, group_id],
        )?;
        Ok(())
    }

    /// Effective permissions of a user: the union of its groups' grants.
    pub fn effective_permissions(&self, user_id: i64) -> Result<HashSet<Permission>, StoreError> {
        let mut statement = self.connection.prepare(
            "SELECT DISTINCT gp.permission
             FROM user_groups ug
             JOIN group_permissions gp ON gp.group_id = ug.group_id
             WHERE ug.user_id = ?1",
        )?;
        let rows = statement.query_map(params![user_id], |row| row.get::<_, String>(0))?;
        let mut permissions = HashSet::new();
        for name in rows {
            if let Some(permission) = Permission::from_str(&name?) {
                permissions.insert(permission);
            }
        }
        Ok(permissions)
    }

    // ----------------------------------------------------------- sessions

    /// Open a session for a user, returning the session token (cookie) and the
    /// CSRF token the client echoes on mutating requests.
    pub fn create_session(&self, user_id: i64) -> Result<SessionTokens, StoreError> {
        let session_token = random_token();
        let csrf_token = random_token();
        let now = now();
        // Store only the hash of the session token: a stolen web.db then yields
        // no usable cookie. The caller keeps the raw token for the cookie.
        self.connection.execute(
            "INSERT INTO sessions (token, user_id, created_at, last_seen_at, csrf_token) VALUES (?1, ?2, ?3, ?3, ?4)",
            params![hash_token(&session_token), user_id, now, csrf_token],
        )?;
        Ok(SessionTokens { session_token, csrf_token })
    }

    /// Resolve a session token to its user id and CSRF token, enforcing idle
    /// and absolute expiry. A valid session has its `last_seen_at` refreshed;
    /// an expired one is deleted and resolves to None. A session whose user has
    /// been disabled resolves to None (the disable takes effect immediately).
    pub fn validate_session(&self, token: &str) -> Result<Option<SessionInfo>, StoreError> {
        let hashed = hash_token(token);
        let row: Option<(i64, i64, i64, String)> = self
            .connection
            .query_row(
                "SELECT s.user_id, s.created_at, s.last_seen_at, s.csrf_token
                 FROM sessions s JOIN users u ON u.id = s.user_id
                 WHERE s.token = ?1 AND u.enabled != 0",
                params![hashed],
                |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?, row.get(3)?)),
            )
            .optional()?;
        let Some((user_id, created_at, last_seen_at, csrf_token)) = row else { return Ok(None) };

        let now = now();
        let expired = now - last_seen_at > SESSION_IDLE_SECS || now - created_at > SESSION_ABSOLUTE_SECS;
        if expired {
            self.delete_session(token)?;
            return Ok(None);
        }
        self.connection.execute("UPDATE sessions SET last_seen_at = ?1 WHERE token = ?2", params![now, hashed])?;
        Ok(Some(SessionInfo { user_id, csrf_token }))
    }

    pub fn delete_session(&self, token: &str) -> Result<(), StoreError> {
        self.connection.execute("DELETE FROM sessions WHERE token = ?1", params![hash_token(token)])?;
        Ok(())
    }

    // ----------------------------------------------------------- audit log

    /// Record a mutating operation. `actor` is the user name (or a marker for
    /// pre-auth events), `action` the method, `target` the path, `status` the
    /// HTTP status the request produced.
    pub fn record_audit(
        &self,
        actor_id: Option<i64>,
        actor: &str,
        action: &str,
        target: &str,
        status: u16,
    ) -> Result<(), StoreError> {
        self.connection.execute(
            "INSERT INTO audit_log (at, actor_id, actor, action, target, status) VALUES (?1, ?2, ?3, ?4, ?5, ?6)",
            params![now(), actor_id, actor, action, target, status as i64],
        )?;
        Ok(())
    }

    /// Delete sessions past their idle/absolute expiry and trim the audit log
    /// to its most recent rows. Cheap; meant to run on a slow timer.
    pub fn purge_expired(&self) -> Result<(), StoreError> {
        let now = now();
        self.connection.execute(
            "DELETE FROM sessions WHERE ?1 - last_seen_at > ?2 OR ?1 - created_at > ?3",
            params![now, SESSION_IDLE_SECS, SESSION_ABSOLUTE_SECS],
        )?;
        self.connection.execute(
            "DELETE FROM audit_log WHERE id NOT IN (SELECT id FROM audit_log ORDER BY id DESC LIMIT ?1)",
            params![AUDIT_RETAIN],
        )?;
        Ok(())
    }

    /// Most recent audit entries, newest first.
    pub fn list_audit(&self, limit: i64) -> Result<Vec<AuditEntry>, StoreError> {
        let mut statement = self.connection.prepare(
            "SELECT at, actor, action, target, status FROM audit_log ORDER BY id DESC LIMIT ?1",
        )?;
        let rows = statement.query_map(params![limit], |row| {
            Ok(AuditEntry {
                at: row.get(0)?,
                actor: row.get(1)?,
                action: row.get(2)?,
                target: row.get(3)?,
                status: row.get::<_, i64>(4)? as u16,
            })
        })?;
        Ok(rows.collect::<Result<_, _>>()?)
    }
}

fn now() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0)
}

/// Insert a user (validating name and password length) on any connection or
/// transaction, returning the new row id.
fn insert_user(connection: &Connection, name: &str, password: &str, enabled: bool) -> Result<i64, StoreError> {
    if name.trim().is_empty() {
        return Err(StoreError::Conflict("empty user name"));
    }
    if password.len() < MIN_PASSWORD_LEN {
        return Err(StoreError::Conflict("password too short"));
    }
    let hash = hash_password(password).map_err(StoreError::Hash)?;
    connection
        .execute(
            "INSERT INTO users (name, password_hash, enabled, created_at) VALUES (?1, ?2, ?3, ?4)",
            params![name, hash, enabled as i64, now()],
        )
        .map_err(|error| match error {
            rusqlite::Error::SqliteFailure(e, _) if e.code == rusqlite::ErrorCode::ConstraintViolation => {
                StoreError::Conflict("user name already exists")
            }
            other => StoreError::Db(other),
        })?;
    Ok(connection.last_insert_rowid())
}

/// Group id by name, creating it if absent, on any connection or transaction.
fn ensure_group_id(connection: &Connection, name: &str) -> Result<i64, StoreError> {
    let existing: Option<i64> = connection
        .query_row("SELECT id FROM groups WHERE name = ?1", params![name], |row| row.get(0))
        .optional()?;
    if let Some(id) = existing {
        return Ok(id);
    }
    connection.execute("INSERT INTO groups (name) VALUES (?1)", params![name])?;
    Ok(connection.last_insert_rowid())
}

/// Number of enabled users with the `users.manage` permission through any of
/// their groups — the population that must never reach zero (lockout).
fn users_manage_holder_count(connection: &Connection) -> Result<i64, StoreError> {
    let count: i64 = connection.query_row(
        "SELECT COUNT(DISTINCT u.id)
         FROM users u
         JOIN user_groups ug ON ug.user_id = u.id
         JOIN group_permissions gp ON gp.group_id = ug.group_id
         WHERE u.enabled != 0 AND gp.permission = ?1",
        params![Permission::UsersManage.as_str()],
        |row| row.get(0),
    )?;
    Ok(count)
}

/// SHA-256 of a token, hex-encoded; what the sessions table stores so a leaked
/// database does not hand over usable cookies.
fn hash_token(token: &str) -> String {
    let digest = Sha256::digest(token.as_bytes());
    let mut hex = String::with_capacity(64);
    for byte in digest {
        hex.push_str(&format!("{byte:02x}"));
    }
    hex
}

/// 256 bits of randomness, hex-encoded, for an unguessable session token.
fn random_token() -> String {
    let mut bytes = [0u8; 32];
    rand::rng().fill_bytes(&mut bytes);
    let mut hex = String::with_capacity(64);
    for byte in bytes {
        hex.push_str(&format!("{byte:02x}"));
    }
    hex
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn create_and_verify_user() {
        let store = AuthStore::open_in_memory().unwrap();
        assert_eq!(store.user_count().unwrap(), 0);
        let id = store.create_user("admin", "s3cret99", true).unwrap();
        assert_eq!(store.user_count().unwrap(), 1);
        assert_eq!(store.verify_login("admin", "s3cret99").unwrap(), Some(id));
        assert_eq!(store.verify_login("admin", "wrong").unwrap(), None);
        assert_eq!(store.verify_login("ghost", "s3cret99").unwrap(), None);
    }

    #[test]
    fn disabled_user_cannot_log_in() {
        let store = AuthStore::open_in_memory().unwrap();
        let id = store.create_user("bob", "password1", true).unwrap();
        store.set_user_enabled(0, id, false).unwrap();
        assert_eq!(store.verify_login("bob", "password1").unwrap(), None);
    }

    #[test]
    fn duplicate_user_name_conflicts() {
        let store = AuthStore::open_in_memory().unwrap();
        store.create_user("dup", "password1", true).unwrap();
        assert!(matches!(store.create_user("dup", "password2", true), Err(StoreError::Conflict(_))));
    }

    #[test]
    fn effective_permissions_union_across_groups() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "password1", true).unwrap();
        let viewers = store.create_group("viewers").unwrap();
        let kickers = store.create_group("kickers").unwrap();
        store.set_group_permissions(viewers, &[Permission::ViewsRead]).unwrap();
        store.set_group_permissions(kickers, &[Permission::PeersKick]).unwrap();
        store.add_user_to_group(user, viewers).unwrap();
        store.add_user_to_group(user, kickers).unwrap();

        let permissions = store.effective_permissions(user).unwrap();
        assert!(permissions.contains(&Permission::ViewsRead));
        assert!(permissions.contains(&Permission::PeersKick));
        assert!(!permissions.contains(&Permission::UsersManage));
    }

    #[test]
    fn removing_from_group_drops_permission() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "password1", true).unwrap();
        let group = store.create_group("g").unwrap();
        store.set_group_permissions(group, &[Permission::AclManage]).unwrap();
        store.add_user_to_group(user, group).unwrap();
        assert!(store.effective_permissions(user).unwrap().contains(&Permission::AclManage));
        store.remove_user_from_group(user, group).unwrap();
        assert!(store.effective_permissions(user).unwrap().is_empty());
    }

    #[test]
    fn session_lifecycle() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("admin", "password1", true).unwrap();
        let tokens = store.create_session(user).unwrap();
        assert_eq!(tokens.session_token.len(), 64);
        assert_eq!(tokens.csrf_token.len(), 64);
        assert_ne!(tokens.session_token, tokens.csrf_token);
        let info = store.validate_session(&tokens.session_token).unwrap().unwrap();
        assert_eq!(info.user_id, user);
        assert_eq!(info.csrf_token, tokens.csrf_token);
        store.delete_session(&tokens.session_token).unwrap();
        assert_eq!(store.validate_session(&tokens.session_token).unwrap(), None);
    }

    #[test]
    fn unknown_session_is_none() {
        let store = AuthStore::open_in_memory().unwrap();
        assert_eq!(store.validate_session("deadbeef").unwrap(), None);
    }

    #[test]
    fn deleting_user_cascades_sessions_and_memberships() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("temp", "password1", true).unwrap();
        let group = store.create_group("g").unwrap();
        store.add_user_to_group(user, group).unwrap();
        let tokens = store.create_session(user).unwrap();
        store.delete_user(0, user).unwrap();
        assert_eq!(store.validate_session(&tokens.session_token).unwrap(), None);
    }

    #[test]
    fn short_password_is_rejected() {
        let store = AuthStore::open_in_memory().unwrap();
        assert!(matches!(store.create_user("a", "", true), Err(StoreError::Conflict(_))));
        assert!(matches!(store.create_user("b", "short", true), Err(StoreError::Conflict(_))));
        assert!(store.create_user("c", "longenough", true).is_ok());
    }

    #[test]
    fn disabling_user_invalidates_session() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "password1", true).unwrap();
        let tokens = store.create_session(user).unwrap();
        assert!(store.validate_session(&tokens.session_token).unwrap().is_some());
        store.set_user_enabled(0, user, false).unwrap();
        assert_eq!(store.validate_session(&tokens.session_token).unwrap(), None);
    }

    #[test]
    fn session_token_is_stored_hashed() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "password1", true).unwrap();
        let tokens = store.create_session(user).unwrap();
        let stored: String =
            store.connection.query_row("SELECT token FROM sessions", [], |row| row.get(0)).unwrap();
        assert_ne!(stored, tokens.session_token);
        assert_eq!(stored, hash_token(&tokens.session_token));
        // The raw token still validates (it is hashed on the way in).
        assert!(store.validate_session(&tokens.session_token).unwrap().is_some());
    }

    fn admin_with_users_manage(store: &AuthStore, name: &str) -> i64 {
        let user = store.create_user(name, "password1", true).unwrap();
        let group = store.create_group(&format!("g-{name}")).unwrap();
        store.set_group_permissions(group, &[Permission::UsersManage]).unwrap();
        store.add_user_to_group(user, group).unwrap();
        user
    }

    #[test]
    fn cannot_delete_or_disable_self() {
        let store = AuthStore::open_in_memory().unwrap();
        let me = admin_with_users_manage(&store, "me");
        assert!(matches!(store.delete_user(me, me), Err(StoreError::Conflict(_))));
        assert!(matches!(store.set_user_enabled(me, me, false), Err(StoreError::Conflict(_))));
    }

    #[test]
    fn cannot_remove_last_users_manage_holder() {
        let store = AuthStore::open_in_memory().unwrap();
        let admin = admin_with_users_manage(&store, "admin");
        let other = store.create_user("other", "password1", true).unwrap();
        // `other` (no users.manage) deleting the only admin would lock everyone out.
        assert!(matches!(store.delete_user(other, admin), Err(StoreError::Conflict(_))));
        assert!(matches!(store.set_user_enabled(other, admin, false), Err(StoreError::Conflict(_))));
    }

    #[test]
    fn can_remove_admin_when_another_holder_exists() {
        let store = AuthStore::open_in_memory().unwrap();
        let first = admin_with_users_manage(&store, "first");
        let second = admin_with_users_manage(&store, "second");
        assert!(store.delete_user(second, first).is_ok());
    }

    #[test]
    fn cannot_strip_users_manage_from_last_group() {
        let store = AuthStore::open_in_memory().unwrap();
        let admin = store.create_user("admin", "password1", true).unwrap();
        let group = store.create_group("admins").unwrap();
        store.set_group_permissions(group, &[Permission::UsersManage]).unwrap();
        store.add_user_to_group(admin, group).unwrap();
        // Removing users.manage from the only group that grants it is refused.
        assert!(matches!(
            store.set_group_permissions(group, &[Permission::ViewsRead]),
            Err(StoreError::Conflict(_))
        ));
        // And deleting that group is refused too.
        assert!(matches!(store.delete_group(group), Err(StoreError::Conflict(_))));
    }

    #[test]
    fn update_password_keeps_current_session_and_drops_others() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "password1", true).unwrap();
        let keep = store.create_session(user).unwrap();
        let other = store.create_session(user).unwrap();
        store.update_password(user, "newpassword", Some(&keep.session_token)).unwrap();
        assert!(store.validate_session(&keep.session_token).unwrap().is_some());
        assert_eq!(store.validate_session(&other.session_token).unwrap(), None);
        assert_eq!(store.verify_login("op", "password1").unwrap(), None);
        assert_eq!(store.verify_login("op", "newpassword").unwrap(), Some(user));
    }

    #[test]
    fn admin_password_reset_drops_all_sessions() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "password1", true).unwrap();
        let session = store.create_session(user).unwrap();
        store.update_password(user, "newpassword", None).unwrap();
        assert_eq!(store.validate_session(&session.session_token).unwrap(), None);
    }

    #[test]
    fn update_password_rejects_short_and_verifies_current() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "password1", true).unwrap();
        assert!(matches!(store.update_password(user, "short", None), Err(StoreError::Conflict(_))));
        assert!(store.verify_user_password(user, "password1").unwrap());
        assert!(!store.verify_user_password(user, "wrong").unwrap());
    }

    #[test]
    fn list_groups_returns_permissions_per_group() {
        let store = AuthStore::open_in_memory().unwrap();
        let a = store.create_group("alpha").unwrap();
        store.create_group("beta").unwrap(); // no permissions
        store.set_group_permissions(a, &[Permission::ViewsRead, Permission::PeersKick]).unwrap();
        let groups = store.list_groups().unwrap();
        assert_eq!(groups.len(), 2);
        assert_eq!(groups[0].name, "alpha");
        assert_eq!(groups[0].permissions, vec!["peers.kick", "views.read"]);
        assert_eq!(groups[1].name, "beta");
        assert!(groups[1].permissions.is_empty());
    }

    #[test]
    fn purge_expired_trims_audit_log() {
        let store = AuthStore::open_in_memory().unwrap();
        for index in 0..3 {
            store.record_audit(None, "x", "POST", &format!("/api/{index}"), 200).unwrap();
        }
        store.purge_expired().unwrap();
        // Recent rows are retained (well under AUDIT_RETAIN).
        assert_eq!(store.list_audit(100).unwrap().len(), 3);
    }

    #[test]
    fn audit_log_records_newest_first() {
        let store = AuthStore::open_in_memory().unwrap();
        store.record_audit(Some(1), "admin", "POST", "/api/peers/5/kick", 200).unwrap();
        store.record_audit(Some(1), "admin", "DELETE", "/api/pins/ghost", 409).unwrap();
        let entries = store.list_audit(10).unwrap();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].target, "/api/pins/ghost");
        assert_eq!(entries[0].status, 409);
        assert_eq!(entries[1].action, "POST");
    }
}
