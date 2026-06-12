//! `web.db` store: users, groups, group permissions, memberships and
//! server-side sessions. Synchronous (rusqlite); the HTTP layer calls it on a
//! blocking task behind a mutex.

use std::collections::HashSet;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use rand::Rng;
use rusqlite::{params, Connection, OptionalExtension};

use super::password::{hash_password, verify_password};
use super::Permission;

/// Session expiry: a session dies after this much inactivity, or this long
/// after creation regardless of activity.
const SESSION_IDLE_SECS: i64 = 12 * 3600;
const SESSION_ABSOLUTE_SECS: i64 = 7 * 24 * 3600;

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
        if name.trim().is_empty() {
            return Err(StoreError::Conflict("empty user name"));
        }
        let hash = hash_password(password).map_err(StoreError::Hash)?;
        self.connection
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
        Ok(self.connection.last_insert_rowid())
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

    pub fn set_user_enabled(&self, user_id: i64, enabled: bool) -> Result<(), StoreError> {
        self.connection.execute("UPDATE users SET enabled = ?1 WHERE id = ?2", params![enabled as i64, user_id])?;
        Ok(())
    }

    pub fn delete_user(&self, user_id: i64) -> Result<(), StoreError> {
        self.connection.execute("DELETE FROM users WHERE id = ?1", params![user_id])?;
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

    /// Get a group id by name, creating it if absent (idempotent bootstrap).
    pub fn ensure_group(&self, name: &str) -> Result<i64, StoreError> {
        let existing: Option<i64> = self
            .connection
            .query_row("SELECT id FROM groups WHERE name = ?1", params![name], |row| row.get(0))
            .optional()?;
        match existing {
            Some(id) => Ok(id),
            None => self.create_group(name),
        }
    }

    pub fn delete_group(&self, group_id: i64) -> Result<(), StoreError> {
        self.connection.execute("DELETE FROM groups WHERE id = ?1", params![group_id])?;
        Ok(())
    }

    pub fn set_group_permissions(&self, group_id: i64, permissions: &[Permission]) -> Result<(), StoreError> {
        self.connection.execute("DELETE FROM group_permissions WHERE group_id = ?1", params![group_id])?;
        for permission in permissions {
            self.connection.execute(
                "INSERT INTO group_permissions (group_id, permission) VALUES (?1, ?2)",
                params![group_id, permission.as_str()],
            )?;
        }
        Ok(())
    }

    pub fn list_groups(&self) -> Result<Vec<Group>, StoreError> {
        let mut statement = self.connection.prepare("SELECT id, name FROM groups ORDER BY name")?;
        let bare: Vec<(i64, String)> =
            statement.query_map([], |row| Ok((row.get(0)?, row.get(1)?)))?.collect::<Result<_, _>>()?;
        let mut groups = Vec::with_capacity(bare.len());
        for (id, name) in bare {
            let mut permission_statement =
                self.connection.prepare("SELECT permission FROM group_permissions WHERE group_id = ?1 ORDER BY permission")?;
            let permissions: Vec<String> =
                permission_statement.query_map(params![id], |row| row.get(0))?.collect::<Result<_, _>>()?;
            groups.push(Group { id, name, permissions });
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
        self.connection.execute(
            "INSERT INTO sessions (token, user_id, created_at, last_seen_at, csrf_token) VALUES (?1, ?2, ?3, ?3, ?4)",
            params![session_token, user_id, now, csrf_token],
        )?;
        Ok(SessionTokens { session_token, csrf_token })
    }

    /// Resolve a session token to its user id and CSRF token, enforcing idle
    /// and absolute expiry. A valid session has its `last_seen_at` refreshed;
    /// an expired one is deleted and resolves to None.
    pub fn validate_session(&self, token: &str) -> Result<Option<SessionInfo>, StoreError> {
        let row: Option<(i64, i64, i64, String)> = self
            .connection
            .query_row(
                "SELECT user_id, created_at, last_seen_at, csrf_token FROM sessions WHERE token = ?1",
                params![token],
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
        self.connection.execute("UPDATE sessions SET last_seen_at = ?1 WHERE token = ?2", params![now, token])?;
        Ok(Some(SessionInfo { user_id, csrf_token }))
    }

    pub fn delete_session(&self, token: &str) -> Result<(), StoreError> {
        self.connection.execute("DELETE FROM sessions WHERE token = ?1", params![token])?;
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
        let id = store.create_user("admin", "s3cret", true).unwrap();
        assert_eq!(store.user_count().unwrap(), 1);
        assert_eq!(store.verify_login("admin", "s3cret").unwrap(), Some(id));
        assert_eq!(store.verify_login("admin", "wrong").unwrap(), None);
        assert_eq!(store.verify_login("ghost", "s3cret").unwrap(), None);
    }

    #[test]
    fn disabled_user_cannot_log_in() {
        let store = AuthStore::open_in_memory().unwrap();
        let id = store.create_user("bob", "pw", true).unwrap();
        store.set_user_enabled(id, false).unwrap();
        assert_eq!(store.verify_login("bob", "pw").unwrap(), None);
    }

    #[test]
    fn duplicate_user_name_conflicts() {
        let store = AuthStore::open_in_memory().unwrap();
        store.create_user("dup", "pw", true).unwrap();
        assert!(matches!(store.create_user("dup", "pw2", true), Err(StoreError::Conflict(_))));
    }

    #[test]
    fn effective_permissions_union_across_groups() {
        let store = AuthStore::open_in_memory().unwrap();
        let user = store.create_user("op", "pw", true).unwrap();
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
        let user = store.create_user("op", "pw", true).unwrap();
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
        let user = store.create_user("admin", "pw", true).unwrap();
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
        let user = store.create_user("temp", "pw", true).unwrap();
        let group = store.create_group("g").unwrap();
        store.add_user_to_group(user, group).unwrap();
        let tokens = store.create_session(user).unwrap();
        store.delete_user(user).unwrap();
        assert_eq!(store.validate_session(&tokens.session_token).unwrap(), None);
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
