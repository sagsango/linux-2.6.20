kswapd Daemon 
[kernel/kswapd.c; ~950ms+ spawn; [C]; Line ~100-300; protected mode C; kswapd thread (PID ~6, spawned by kthreadd via kthread_run(kswapd_main, NULL, "kswapd0")); runs in background to reclaim memory pages when free mem < watermark (low/min); scans zones (DMA/Normal/HighMem), calls shrink_zone to free pages via LRU (drop caches, swap out); globals: struct task_struct *kswapd_task[NR_CPUS], wait_queue_head_t kswapd_wait (for wake on lowmem), unsigned long watermark[NR_WMARK]; 2.6.20: Single kswapd0 (no per-node, MAX_NUMNODES=1); direct reclaim in shrink_zone (no kswapd per-CPU); state: PE=1 PG=1 WP=1 IRQ=1 PREEMPT=1 RUNNING; async from steady state phase 44; runs ~every 10s or on wake; output: Printk "kswapd0: page allocation failure" if OOM, "Out of Memory: Kill process %d (%s)" if direct reclaim fail; error: Stuck loop (no reclaim) -> OOM killer activate; rationale: Background mem pressure relief (prevent OOM, balance zones)]
    |
    +--> Thread Entry: int kswapd_main(void *unused) [Line ~100; unused=NULL from kthread_run; current = kswapd_task[0]; PID~6 state=TASK_INTERRUPTIBLE prio=120 nice=0]
          |
          +--> Wait Queue Init (Line ~105): init_waitqueue_head(&kswapd_wait); // Global wait for wake on lowmem; spin_lock_init(&kswapd_wait.lock); INIT_LIST_HEAD(&kswapd_wait.task_list);
          | |
          | +--> Global: wait_queue_head_t kswapd_wait; [in mm/page_alloc.c; Rationale: Sleep kswapd until wake_all_kswapds (when zone_pageset low); 2.6.20: Single wq (no per-node); error: Lock init fail (no mem) -> corrupt wait (deadlock on wake)]
          | | [e.g., kswapd_wait.lock = __SPIN_LOCK_UNLOCKED; task_list empty; used in try_to_free_pages (if !wake_kswapd) wake_up_interruptible(&kswapd_wait); 2.6.20: No per-CPU wq]
          |
          +--> Main Loop: for (;;) { if (kthread_should_stop()) break; set_current_state(TASK_INTERRUPTIBLE); if (try_to_free_pages(current->mm)) { /* Success */ } else { /* Wait */ schedule(); continue; } set_current_state(TASK_RUNNING); } // Wait wake, reclaim if pressure
          | |
          | +--> kthread_should_stop Check: test_bit(KTHREAD_SHOULD_STOP, &current->flags); [Rationale: Exit if kthread_stop (kill -TERM PID6); 2.6.20: Inline test_bit(17, flags); global no; error: Bit corrupt -> wrong exit (leak thread)]
          | | [set_current_state(TASK_INTERRUPTIBLE=2); schedule() yields to idle until wake_all_kswapds; 2.6.20: No RT priority; printk none]
          |
          +--> Memory Reclaim Call (Line ~150): while (try_to_free_pages(current->mm, gfp_mask, order)) { /* Reclaim */ } // try_to_free_pages: for each zone if low, shrink_zone(zone, sc, priority=12); sc.nr_to_reclaim = SWAP_CLUSTER_MAX * 32;
          | |
          | +--> Sub-Func try_to_free_pages (mm/vmscan.c Line ~1000-1200): unsigned long ret = 0; struct scan_control sc = { .nr_to_reclaim = 1 << order, .gfp_mask = gfp_mask, .priority = DEF_PRIORITY=12, .may_writepage = 1, .may_unmap = 1, .may_swap = 1, .swap_cluster_max = SWAP_CLUSTER_MAX=32, .reclaim_mode = DEACTIVATE_ANON|DEACTIVATE_FILE, .target_mem_cgroup = NULL }; for (int zid = 0; zid <= ZONE_NORMAL; zid++) { if (sc.nr_to_reclaim <= 0) break; struct zone *zone = &contig_page_data.node_zones[zid]; if (zone->present_pages == 0) continue; unsigned long scanned = shrink_zone(zone, &sc); ret += scanned; } return ret > 0; // Success if reclaimed
          | | [Rationale: Reclaim from LRU lists (active/inactive anon/file); priority 12-0 (aggressive); 2.6.20: shrink_zone calls shrink_cache (drop pagecache), shrink_slab, try_to_swap_out; global struct zone *zone_table[MAX_NUMNODES*MAX_NR_ZONES]; error: No reclaim (all locked) -> ret=0 wake OOM killer]
          | | [Global: struct scan_control { unsigned long nr_to_reclaim; unsigned int gfp_mask; int priority; int may_writepage, may_unmap, may_swap; int swap_cluster_max; unsigned int reclaim_mode; struct mem_cgroup *target_mem_cgroup; }; [Rationale: Params for shrink (gfp= GFP_KERNEL=0xD0 atomic wait); DEF_PRIORITY=12 (mild); 2.6.20: No memcg (target=NULL); shrink_zone: nr[3] = SWAP_CLUSTER_MAX << (DEF_PRIORITY - priority); for (int pass=0; pass<4 && sc.nr_to_reclaim; pass++) shrink_cache_zone(zone, pass);]
          | | [e.g., sc.nr_to_reclaim=32; gfp_mask=GFP_KERNEL; priority=12; shrink_cache_zone: for (type=0; type<2; type++) { list_for_each_entry_reverse(page, &zone->lru[type], lru) { if (PageAnon(page)) shrink_page_list(&page, zone, &sc); else try_to_free_swap(page, &sc); if (sc.nr_to_reclaim <=0) break; } } ; printk "kswapd reclaimed %lu pages" if debug]
          |
          +--> 2.6.20 Specifics: kswapd_task[0] = current; no per-node kswapd (MAX_NUMNODES=1); wake_all_kswapds in balance_pgdat (if zone->lowmem_reserve < watermark); shrink_slab calls shrink_icache_memory, prune_dcache_sb, etc.; no vmpressure (added 3.10); globals exported kswapd_wait (module_param sysctl vm.swappiness=60)
          | |
          | +--> [Overall: ~950ms+ ongoing (wake ~10s or pressure, reclaim ~100ms/scan); silent; ERR: Reclaim 0 (all locked) -> OOM killer select_victim_task (kill low OOM_score); Rationale: Background mem pressure (keep free > watermark, balance zones); next kernel_init parallel phase 31]
    |
    v
kernel_init() Thread Start (Phase 31: main.c; ~420-430ms; [C]; PID1 do_basic_setup mount exec; 2.6.20: SysV init)
