.
├── capability.c
├── commoncap.c
├── dummy.c
├── inode.c
├── Kconfig
├── keys
│   ├── compat.c
│   ├── internal.h
│   ├── key.c
│   ├── keyctl.c
│   ├── keyring.c
│   ├── Makefile
│   ├── permission.c
│   ├── proc.c
│   ├── process_keys.c
│   ├── request_key_auth.c
│   ├── request_key.c
│   └── user_defined.c
├── Makefile
├── NOTES.md
├── root_plug.c
├── security.c
└── selinux
    ├── avc.c
    ├── exports.c
    ├── hooks.c
    ├── include
    │   ├── av_inherit.h
    │   ├── av_perm_to_string.h
    │   ├── av_permissions.h
    │   ├── avc_ss.h
    │   ├── avc.h
    │   ├── class_to_string.h
    │   ├── common_perm_to_string.h
    │   ├── conditional.h
    │   ├── flask.h
    │   ├── initial_sid_to_string.h
    │   ├── netif.h
    │   ├── objsec.h
    │   ├── security.h
    │   ├── selinux_netlabel.h
    │   └── xfrm.h
    ├── Kconfig
    ├── Makefile
    ├── netif.c
    ├── netlink.c
    ├── nlmsgtab.c
    ├── selinuxfs.c
    ├── ss
    │   ├── avtab.c
    │   ├── avtab.h
    │   ├── conditional.c
    │   ├── conditional.h
    │   ├── constraint.h
    │   ├── context.h
    │   ├── ebitmap.c
    │   ├── ebitmap.h
    │   ├── hashtab.c
    │   ├── hashtab.h
    │   ├── Makefile
    │   ├── mls_types.h
    │   ├── mls.c
    │   ├── mls.h
    │   ├── policydb.c
    │   ├── policydb.h
    │   ├── services.c
    │   ├── services.h
    │   ├── sidtab.c
    │   ├── sidtab.h
    │   ├── symtab.c
    │   └── symtab.h
    └── xfrm.c

5 directories, 68 files
