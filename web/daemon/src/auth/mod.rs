//! Browser authentication: local users and groups, permission classes and
//! server-side sessions, all in a dedicated `web.db` (the hub's hub.db and
//! IdentityStorePort stay untouched). The daemon holds full admin power over
//! the hub socket, so authorization is enforced here, in the web layer.

pub mod password;
pub mod store;

/// Operation classes a group may be granted. The effective permission of a
/// user is the union across the groups it belongs to.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Permission {
    ViewsRead,
    PeersKick,
    InterfacesConfig,
    PinsManage,
    AclManage,
    UsersManage,
}

impl Permission {
    pub const ALL: [Permission; 6] = [
        Permission::ViewsRead,
        Permission::PeersKick,
        Permission::InterfacesConfig,
        Permission::PinsManage,
        Permission::AclManage,
        Permission::UsersManage,
    ];

    pub fn as_str(self) -> &'static str {
        match self {
            Permission::ViewsRead => "views.read",
            Permission::PeersKick => "peers.kick",
            Permission::InterfacesConfig => "interfaces.config",
            Permission::PinsManage => "pins.manage",
            Permission::AclManage => "acl.manage",
            Permission::UsersManage => "users.manage",
        }
    }

    pub fn from_str(value: &str) -> Option<Permission> {
        Permission::ALL.into_iter().find(|p| p.as_str() == value)
    }
}
