6835:drivers/acpi/namespace
6836:drivers/acpi/namespace/nsxfobj.c
6837:drivers/acpi/namespace/nsinit.c
6838:drivers/acpi/namespace/nsaccess.c
6839:drivers/acpi/namespace/nsparse.c
6840:drivers/acpi/namespace/nsobject.c
6841:drivers/acpi/namespace/nsxfname.c
6842:drivers/acpi/namespace/nsdumpdv.c
6843:drivers/acpi/namespace/Makefile
6844:drivers/acpi/namespace/nseval.c
6845:drivers/acpi/namespace/nssearch.c
6846:drivers/acpi/namespace/nsutils.c
6847:drivers/acpi/namespace/nsload.c
6848:drivers/acpi/namespace/nsxfeval.c
6849:drivers/acpi/namespace/nsalloc.c
6850:drivers/acpi/namespace/nsnames.c
6851:drivers/acpi/namespace/nswalk.c
6852:drivers/acpi/namespace/nsdump.c
12224:fs/nfs/nfs4namespace.c
12240:fs/nfs/namespace.c
13060:fs/namespace.c
17232:include/linux/pid_namespace.h
17983:include/linux/mnt_namespace.h
20657:scripts/namespace.pl





linux-2.6.20 ❱❱❱ grep --color=none -r namespace include
include/asm-arm26/types.h:9: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-arm26/posix_types.h:18: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-sparc64/types.h:18: * _xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-sparc64/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-sparc64/perfctr.h:57:/* I don't want the kernel's namespace to be polluted with this
include/asm-cris/types.h:9: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-cris/posix_types.h:11: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-s390/types.h:17: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-s390/posix_types.h:14: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-avr32/types.h:16: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-avr32/posix_types.h:13: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-frv/types.h:20: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-frv/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/acpi/acdebug.h:97:void acpi_db_dump_namespace(char *start_arg, char *depth_arg);
include/acpi/acdebug.h:99:void acpi_db_dump_namespace_by_owner(char *owner_arg, char *depth_arg);
include/acpi/acdebug.h:108:acpi_status acpi_db_find_name_in_namespace(char *name_arg);
include/acpi/acdebug.h:215:struct acpi_namespace_node *acpi_db_local_ns_lookup(char *name);
include/acpi/acpixf.h:124:acpi_walk_namespace(acpi_object_type type,
include/acpi/acutils.h:347:acpi_ut_evaluate_object(struct acpi_namespace_node *prefix_node,
include/acpi/acutils.h:354:				struct acpi_namespace_node *device_node,
include/acpi/acutils.h:358:acpi_ut_execute_HID(struct acpi_namespace_node *device_node,
include/acpi/acutils.h:362:acpi_ut_execute_CID(struct acpi_namespace_node *device_node,
include/acpi/acutils.h:366:acpi_ut_execute_STA(struct acpi_namespace_node *device_node,
include/acpi/acutils.h:370:acpi_ut_execute_UID(struct acpi_namespace_node *device_node,
include/acpi/acutils.h:374:acpi_ut_execute_sxds(struct acpi_namespace_node *device_node, u8 * highest);
include/acpi/acutils.h:491:			      struct acpi_namespace_node *obj_handle,
include/acpi/acdispat.h:106:		     struct acpi_namespace_node *region_node,
include/acpi/acdispat.h:111:			  struct acpi_namespace_node *region_node,
include/acpi/acdispat.h:116:			   struct acpi_namespace_node *region_node,
include/acpi/acdispat.h:128: * dsload - Parser/Interpreter interface, namespace load callbacks
include/acpi/acdispat.h:179:			     struct acpi_namespace_node **node);
include/acpi/acdispat.h:186:acpi_status acpi_ds_parse_method(struct acpi_namespace_node *node);
include/acpi/acdispat.h:202:acpi_ds_begin_method_execution(struct acpi_namespace_node *method_node,
include/acpi/acdispat.h:214:			   struct acpi_namespace_node *start_node);
include/acpi/acdispat.h:238:		    struct acpi_namespace_node *node,
include/acpi/acdispat.h:276:acpi_ds_scope_stack_push(struct acpi_namespace_node *node,
include/acpi/acdispat.h:304:		      struct acpi_namespace_node *method_node,
include/acpi/acconfig.h:149:/* Names within the namespace are 4 bytes long */
include/acpi/acglobal.h:123: * Create the predefined _OSI method in the namespace? Default is TRUE
include/acpi/acglobal.h:227:ACPI_EXTERN acpi_cache_t *acpi_gbl_namespace_cache;
include/acpi/acglobal.h:283:ACPI_EXTERN struct acpi_namespace_node acpi_gbl_root_node_struct;
include/acpi/acglobal.h:284:ACPI_EXTERN struct acpi_namespace_node *acpi_gbl_root_node;
include/acpi/acglobal.h:285:ACPI_EXTERN struct acpi_namespace_node *acpi_gbl_fadt_gpe_device;
include/acpi/acglobal.h:375:ACPI_EXTERN struct acpi_namespace_node *acpi_gbl_db_scope_node;
include/acpi/acparser.h:182:			struct acpi_namespace_node *start_node,
include/acpi/platform/aclinux.h:72:/* Full namespace pathname length limit - arbitrary */
include/acpi/acevents.h:61:u8 acpi_ev_is_notify_object(struct acpi_namespace_node *node);
include/acpi/acevents.h:72:acpi_ev_queue_notify_request(struct acpi_namespace_node *node,
include/acpi/acevents.h:103:acpi_ev_create_gpe_block(struct acpi_namespace_node *gpe_device,
include/acpi/acevents.h:111:acpi_ev_initialize_gpe_block(struct acpi_namespace_node *gpe_device,
include/acpi/acevents.h:153:acpi_ev_install_space_handler(struct acpi_namespace_node *node,
include/acpi/acevents.h:159:acpi_ev_execute_reg_methods(struct acpi_namespace_node *node,
include/acpi/acresrc.h:173:acpi_rs_get_prt_method_data(struct acpi_namespace_node *node,
include/acpi/acresrc.h:177:acpi_rs_get_crs_method_data(struct acpi_namespace_node *node,
include/acpi/acresrc.h:181:acpi_rs_get_prs_method_data(struct acpi_namespace_node *node,
include/acpi/acresrc.h:189:acpi_rs_set_srs_method_data(struct acpi_namespace_node *node,
include/acpi/acstruct.h:99:	struct acpi_namespace_node arguments[ACPI_METHOD_NUM_ARGS];	/* Control method arguments */
include/acpi/acstruct.h:100:	struct acpi_namespace_node local_variables[ACPI_METHOD_NUM_LOCALS];	/* Control method locals */
include/acpi/acstruct.h:107:	struct acpi_namespace_node *deferred_node;	/* Used when executing deferred opcodes */
include/acpi/acstruct.h:110:	struct acpi_namespace_node *method_call_node;	/* Called method Node */
include/acpi/acstruct.h:113:	struct acpi_namespace_node *method_node;	/* Method node if running a method. */
include/acpi/acstruct.h:182:	struct acpi_namespace_node *prefix_node;
include/acpi/acstruct.h:186:	struct acpi_namespace_node *resolved_node;
include/acpi/acnames.h:47:/* Method names - these methods can appear anywhere in the namespace */
include/acpi/acnames.h:64:/* Method names - these methods must appear at the namespace root */
include/acpi/acnames.h:72:/* Definitions of the predefined namespace names  */
include/acpi/acnamesp.h:82:acpi_status acpi_ns_load_namespace(void);
include/acpi/acnamesp.h:86:		   struct acpi_namespace_node *node);
include/acpi/acnamesp.h:89: * nswalk - walk the namespace
include/acpi/acnamesp.h:92:acpi_ns_walk_namespace(acpi_object_type type,
include/acpi/acnamesp.h:99:struct acpi_namespace_node *acpi_ns_get_next_node(acpi_object_type type,
include/acpi/acnamesp.h:100:						  struct acpi_namespace_node
include/acpi/acnamesp.h:102:						  struct acpi_namespace_node
include/acpi/acnamesp.h:110:		    struct acpi_namespace_node *scope);
include/acpi/acnamesp.h:116: * nsaccess - Top-level namespace access
include/acpi/acnamesp.h:127:	       struct acpi_namespace_node **ret_node);
include/acpi/acnamesp.h:132:struct acpi_namespace_node *acpi_ns_create_node(u32 name);
include/acpi/acnamesp.h:134:void acpi_ns_delete_node(struct acpi_namespace_node *node);
include/acpi/acnamesp.h:137:acpi_ns_delete_namespace_subtree(struct acpi_namespace_node *parent_handle);
include/acpi/acnamesp.h:139:void acpi_ns_delete_namespace_by_owner(acpi_owner_id owner_id);
include/acpi/acnamesp.h:141:void acpi_ns_detach_object(struct acpi_namespace_node *node);
include/acpi/acnamesp.h:143:void acpi_ns_delete_children(struct acpi_namespace_node *parent);
include/acpi/acnamesp.h:184:acpi_ns_build_external_path(struct acpi_namespace_node *node,
include/acpi/acnamesp.h:187:char *acpi_ns_get_external_pathname(struct acpi_namespace_node *node);
include/acpi/acnamesp.h:196:acpi_ns_pattern_match(struct acpi_namespace_node *obj_node, char *search_for);
include/acpi/acnamesp.h:199:acpi_ns_get_node(struct acpi_namespace_node *prefix_node,
include/acpi/acnamesp.h:201:		 u32 flags, struct acpi_namespace_node **out_node);
include/acpi/acnamesp.h:203:acpi_size acpi_ns_get_pathname_length(struct acpi_namespace_node *node);
include/acpi/acnamesp.h:206: * nsobject - Object management for namespace nodes
include/acpi/acnamesp.h:209:acpi_ns_attach_object(struct acpi_namespace_node *node,
include/acpi/acnamesp.h:213:						       acpi_namespace_node
include/acpi/acnamesp.h:221:acpi_ns_attach_data(struct acpi_namespace_node *node,
include/acpi/acnamesp.h:225:acpi_ns_detach_data(struct acpi_namespace_node *node,
include/acpi/acnamesp.h:229:acpi_ns_get_attached_data(struct acpi_namespace_node *node,
include/acpi/acnamesp.h:238:			 struct acpi_namespace_node *node,
include/acpi/acnamesp.h:241:			 u32 flags, struct acpi_namespace_node **ret_node);
include/acpi/acnamesp.h:245:			 struct acpi_namespace_node *node,
include/acpi/acnamesp.h:247:			 struct acpi_namespace_node **ret_node);
include/acpi/acnamesp.h:251:		     struct acpi_namespace_node *parent_node,
include/acpi/acnamesp.h:252:		     struct acpi_namespace_node *node, acpi_object_type type);
include/acpi/acnamesp.h:259:acpi_object_type acpi_ns_get_type(struct acpi_namespace_node *node);
include/acpi/acnamesp.h:272:			    struct acpi_namespace_node *node,
include/acpi/acnamesp.h:275:void acpi_ns_print_node_pathname(struct acpi_namespace_node *node, char *msg);
include/acpi/acnamesp.h:288:struct acpi_namespace_node *acpi_ns_map_handle_to_node(acpi_handle handle);
include/acpi/acnamesp.h:290:acpi_handle acpi_ns_convert_entry_to_handle(struct acpi_namespace_node *node);
include/acpi/acnamesp.h:294:struct acpi_namespace_node *acpi_ns_get_parent_node(struct acpi_namespace_node
include/acpi/acnamesp.h:297:struct acpi_namespace_node *acpi_ns_get_next_valid_node(struct
include/acpi/acnamesp.h:298:							acpi_namespace_node
include/acpi/actbl1.h:228:	u8 ec_id[1];		/* Full namepath of the EC in the ACPI namespace */
include/acpi/acmacros.h:408: * An struct acpi_namespace_node * can appear in some contexts,
include/acpi/acobject.h:70: * position in both the struct acpi_namespace_node and union acpi_operand_object
include/acpi/acobject.h:125:	struct acpi_namespace_node *node;	/* Link back to parent node */
include/acpi/acobject.h:129:	ACPI_OBJECT_COMMON_HEADER struct acpi_namespace_node *node;	/* Link back to parent node */
include/acpi/acobject.h:153:	struct acpi_namespace_node *node;	/* Containing namespace node */
include/acpi/acobject.h:159:	struct acpi_namespace_node *node;	/* Containing namespace node */
include/acpi/acobject.h:229:	struct acpi_namespace_node      *node;              /* Link back to parent node */\
include/acpi/acobject.h:272:	ACPI_OBJECT_COMMON_HEADER struct acpi_namespace_node *node;	/* Parent device */
include/acpi/acobject.h:281:	struct acpi_namespace_node *node;	/* Parent device */
include/acpi/acobject.h:306:	struct acpi_namespace_node *node;
include/acpi/acobject.h:319:	ACPI_OBJECT_COMMON_HEADER struct acpi_namespace_node *method_REG;	/* _REG method for this region (if any) */
include/acpi/acobject.h:325:/* Additional data that can be attached to namespace nodes */
include/acpi/acobject.h:405:	struct acpi_namespace_node node;
include/acpi/aclocal.h:123:/* Owner IDs are used to track namespace nodes for selective deletion */
include/acpi/aclocal.h:181: * position in both the struct acpi_namespace_node and union acpi_operand_object
include/acpi/aclocal.h:184:struct acpi_namespace_node {
include/acpi/aclocal.h:191:	struct acpi_namespace_node *child;	/* First child */
include/acpi/aclocal.h:192:	struct acpi_namespace_node *peer;	/* Peer. Parent if ANOBJ_END_OF_PEER_LIST set */
include/acpi/aclocal.h:233:	u8 loaded_into_namespace;
include/acpi/aclocal.h:248:	struct acpi_namespace_node *node;
include/acpi/aclocal.h:280:	struct acpi_namespace_node *region_node;
include/acpi/aclocal.h:281:	struct acpi_namespace_node *field_node;
include/acpi/aclocal.h:282:	struct acpi_namespace_node *register_node;
include/acpi/aclocal.h:283:	struct acpi_namespace_node *data_register_node;
include/acpi/aclocal.h:337:	struct acpi_namespace_node *method_node;	/* Method node for this GPE level (saved) */
include/acpi/aclocal.h:341:	struct acpi_namespace_node *method_node;	/* Method node for this GPE level */
include/acpi/aclocal.h:371:	struct acpi_namespace_node *node;
include/acpi/aclocal.h:392:	struct acpi_namespace_node *gpe_device;
include/acpi/aclocal.h:478: * Scope state - current scope during namespace lookups
include/acpi/aclocal.h:481:	ACPI_STATE_COMMON struct acpi_namespace_node *node;
include/acpi/aclocal.h:524:	ACPI_STATE_COMMON struct acpi_namespace_node *node;
include/acpi/aclocal.h:588:	struct acpi_namespace_node      *node;          /* For use by interpreter */\
include/acpi/aclocal.h:670:	struct acpi_namespace_node *start_node;
include/acpi/acinterp.h:52:#define ACPI_EXD_NSOFFSET(f)        (u8) ACPI_OFFSET (struct acpi_namespace_node,f)
include/acpi/acinterp.h:349: * exresnte - resolve namespace node
include/acpi/acinterp.h:352:acpi_ex_resolve_node_to_value(struct acpi_namespace_node **stack_ptr,
include/acpi/acinterp.h:379:void acpi_ex_dump_namespace_node(struct acpi_namespace_node *node, u32 flags);
include/acpi/acinterp.h:400:			     struct acpi_namespace_node *node,
include/acpi/acinterp.h:445:				 struct acpi_namespace_node *node);
include/asm-h8300/types.h:17: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-h8300/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-i386/types.h:9: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-i386/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-m32r/types.h:13: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-m32r/posix_types.h:10: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-arm/types.h:9: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-arm/posix_types.h:18: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-parisc/types.h:9: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-parisc/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-sh/types.h:9: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-sh/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-xtensa/types.h:19: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-xtensa/posix_types.h:18: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-ia64/types.h:31: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-ia64/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-ia64/serial.h:17: * All legacy serial ports should be enumerated via ACPI namespace, so
include/asm-v850/me2.h:25:/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
include/asm-v850/teg.h:36:/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).
include/asm-v850/types.h:17: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-v850/ma1.h:25:/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
include/asm-v850/anna.h:65:/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
include/asm-v850/as85ep1.h:90:/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
include/asm-m68k/raw_io.h:4: * 10/20/00 RZ: - created from bits of io.h and ide.h to cleanup namespace
include/asm-m68k/types.h:17: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-m68k/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-powerpc/termbits.h:18: * concerning namespace pollution.
include/asm-powerpc/types.h:26: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-powerpc/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/linux/init_task.h:10:#include <linux/pid_namespace.h>
include/linux/gfp.h:102: * There is only one page-allocator function, and two main namespaces to
include/linux/pid_namespace.h:18:struct pid_namespace {
include/linux/pid_namespace.h:25:extern struct pid_namespace init_pid_ns;
include/linux/pid_namespace.h:27:static inline void get_pid_ns(struct pid_namespace *ns)
include/linux/pid_namespace.h:35:static inline void put_pid_ns(struct pid_namespace *ns)
include/linux/ext3_jbd.h:49:/* Delete operations potentially hit one directory's namespace plus an
include/linux/elfnote.h:13: * the data, and is considered to be within the "name" namespace (so
include/linux/byteorder/generic.h:89: * outside of it, we must avoid POSIX namespace pollution...
include/linux/ncp_fs.h:79:	int		namespace;
include/linux/ncp_fs.h:239:#define ncp_namespace(i)	(NCP_SERVER(i)->name_space[NCP_FINFO(i)->volNumber])
include/linux/ncp_fs.h:244:	int ns = ncp_namespace(i);
include/linux/ncp_fs.h:256:#define ncp_preserve_case(i)	(ncp_namespace(i) != NW_NS_DOS)
include/linux/ncp_fs.h:261:	return ncp_namespace(i) == NW_NS_NFS;
include/linux/ipc.h:74:struct ipc_namespace {
include/linux/ipc.h:91:extern struct ipc_namespace init_ipc_ns;
include/linux/ipc.h:102:extern int unshare_ipcs(unsigned long flags, struct ipc_namespace **ns);
include/linux/ipc.h:110:static inline struct ipc_namespace *get_ipc_ns(struct ipc_namespace *ns)
include/linux/ipc.h:119:static inline void put_ipc_ns(struct ipc_namespace *ns)
include/linux/ncp_mount.h:20:#define NCP_MOUNT_NO_OS2	0x0008	/* do not use OS/2 (LONG) namespace */
include/linux/ncp_mount.h:21:#define NCP_MOUNT_NO_NFS	0x0010	/* do not use NFS namespace */
include/linux/nsproxy.h:7:struct mnt_namespace;
include/linux/nsproxy.h:8:struct uts_namespace;
include/linux/nsproxy.h:9:struct ipc_namespace;
include/linux/nsproxy.h:10:struct pid_namespace;
include/linux/nsproxy.h:14: * namespaces - fs (mount), uts, network, sysvipc, etc.
include/linux/nsproxy.h:17: * The count for each namespace, then, will be the number
include/linux/nsproxy.h:20: * The nsproxy is shared by tasks which share all namespaces.
include/linux/nsproxy.h:21: * As soon as a single namespace is cloned or unshared, the
include/linux/nsproxy.h:27:	struct uts_namespace *uts_ns;
include/linux/nsproxy.h:28:	struct ipc_namespace *ipc_ns;
include/linux/nsproxy.h:29:	struct mnt_namespace *mnt_ns;
include/linux/nsproxy.h:30:	struct pid_namespace *pid_ns;
include/linux/nsproxy.h:34:struct nsproxy *dup_namespaces(struct nsproxy *orig);
include/linux/nsproxy.h:35:int copy_namespaces(int flags, struct task_struct *tsk);
include/linux/nsproxy.h:36:void get_task_namespaces(struct task_struct *tsk);
include/linux/nsproxy.h:46:static inline void exit_task_namespaces(struct task_struct *p)
include/linux/utsname.h:40:struct uts_namespace {
include/linux/utsname.h:44:extern struct uts_namespace init_uts_ns;
include/linux/utsname.h:46:static inline void get_uts_ns(struct uts_namespace *ns)
include/linux/utsname.h:53:				struct uts_namespace **new_uts);
include/linux/utsname.h:57:static inline void put_uts_ns(struct uts_namespace *ns)
include/linux/utsname.h:63:			struct uts_namespace **new_uts)
include/linux/utsname.h:75:static inline void put_uts_ns(struct uts_namespace *ns)
include/linux/sched.h:18:#define CLONE_NEWNS	0x00020000	/* New namespace group? */
include/linux/sched.h:790:struct uts_namespace;
include/linux/sched.h:934:/* namespaces */
include/linux/mount.h:23:struct mnt_namespace;
include/linux/mount.h:56:	struct mnt_namespace *mnt_ns;	/* containing namespace */
include/linux/ext4_jbd2.h:54:/* Delete operations potentially hit one directory's namespace plus an
include/linux/nfs_fs.h:409: * linux/fs/nfs/namespace.c
include/linux/mnt_namespace.h:9:struct mnt_namespace {
include/linux/mnt_namespace.h:18:extern void __put_mnt_ns(struct mnt_namespace *ns);
include/linux/mnt_namespace.h:19:extern struct mnt_namespace *dup_mnt_ns(struct task_struct *,
include/linux/mnt_namespace.h:22:static inline void put_mnt_ns(struct mnt_namespace *ns)
include/linux/mnt_namespace.h:31:	struct mnt_namespace *ns = p->nsproxy->mnt_ns;
include/linux/mnt_namespace.h:36:static inline void get_mnt_ns(struct mnt_namespace *ns)
include/asm-x86_64/types.h:9: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-x86_64/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-sparc/types.h:6: * _xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-sparc/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-sparc/perfctr.h:57:/* I don't want the kernel's namespace to be polluted with this
include/asm-alpha/termbits.h:13: * concerning namespace pollution.
include/asm-alpha/types.h:17: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-alpha/posix_types.h:6: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-mips/types.h:17: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-mips/posix_types.h:16: * be a little careful about namespace pollution etc.  Also, we cannot
include/asm-mips/gt64240.h:699:#if 0 /* Disabled because PCI_* namespace belongs to PCI subsystem ... */
include/asm-sh64/types.h:20: * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
include/asm-sh64/posix_types.h:15: * be a little careful about namespace pollution etc.  Also, we cannot
