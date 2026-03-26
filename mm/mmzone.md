/******************************************************************************
 * FILE: linux-2.6.20/mm/mmzone.c
 *
 * TITLE:
 *   FULL IDE NOTES — DETAILED SUMMARY + BACKGROUND + POINTERS + FLOW + CONCEPTS
 *
 * GOAL:
 *   Explain this source file the same way you would want inside an IDE:
 *   first the big-picture summary, then the background, then the code-level
 *   walkthrough, then the flows and concepts.
 *
 * SOURCE FILE:
 *   linux/mm/mmzone.c
 *
 ******************************************************************************/

/******************************************************************************
 * 0. DETAILED SUMMARY FIRST
 *
 * This file is tiny, but it exists for a very important reason:
 *
 * Linux memory management often needs to scan memory in this order:
 *
 *      machine
 *        -> NUMA nodes
 *             -> zones inside each node
 *
 * Instead of making every subsystem manually write:
 *
 *      for each online node
 *          for each zone in that node
 *
 * this file provides the basic iteration helpers.
 *
 * So this file is NOT doing:
 *      - page allocation
 *      - reclaim
 *      - buddy management
 *      - page fault handling
 *      - LRU balancing
 *      - zone watermarks
 *
 * It IS doing:
 *      - "give me the first online pgdat"
 *      - "give me the next online pgdat"
 *      - "given this zone, give me the next zone in the whole machine"
 *
 * In other words:
 *
 *      mmzone.c is an iteration utility layer for node/zone traversal.
 *
 *
 * WHY THAT MATTERS:
 *
 * Many core VM paths need to walk the system's zones:
 *
 *      - allocator paths
 *      - reclaim code
 *      - statistics code
 *      - setup / initialization code
 *      - zonelist construction
 *
 * These subsystems need a clean, standard way to move across:
 *
 *      Node0.Zone0 -> Node0.Zone1 -> Node0.Zone2 -> Node1.Zone0 -> ...
 *
 * This file gives exactly that.
 *
 *
 * MOST IMPORTANT IDEA:
 *
 *      struct pglist_data   = one NUMA node
 *      struct zone          = one memory zone inside a node
 *
 * and this file provides traversal from one to the next.
 *
 ******************************************************************************/

/******************************************************************************
 * 1. BACKGROUND — WHY LINUX NEEDS NODES AND ZONES
 *
 * --------------------------------------------------------------------------
 * 1.1 What is a NUMA node?
 * --------------------------------------------------------------------------
 *
 * On NUMA systems, memory is not equally "close" to every CPU.
 *
 * Example:
 *
 *      CPU package / socket A has local RAM bank A
 *      CPU package / socket B has local RAM bank B
 *
 * Accessing local memory is cheaper/faster than remote memory.
 *
 * Linux groups memory per NUMA node.
 *
 * One node is represented by:
 *
 *      struct pglist_data   (often called "pgdat")
 *
 *
 * --------------------------------------------------------------------------
 * 1.2 What is a zone?
 * --------------------------------------------------------------------------
 *
 * Inside a node, memory is further divided into zones.
 *
 * Why?
 *
 * Because not all physical memory can satisfy all requests.
 *
 * Historically, some allocations needed:
 *
 *      - low DMA reachable memory
 *      - normal directly mapped memory
 *      - high memory
 *
 * So each node contains multiple zones such as:
 *
 *      ZONE_DMA
 *      ZONE_NORMAL
 *      ZONE_HIGHMEM
 *
 * exact zones depend on architecture/configuration.
 *
 *
 * --------------------------------------------------------------------------
 * 1.3 Hierarchy
 * --------------------------------------------------------------------------
 *
 * Physical memory hierarchy in Linux looks like:
 *
 *      System
 *        -> Node 0 (pgdat)
 *             -> zone 0
 *             -> zone 1
 *             -> zone 2
 *
 *        -> Node 1 (pgdat)
 *             -> zone 0
 *             -> zone 1
 *             -> zone 2
 *
 *        -> Node 2 (pgdat)
 *             -> zone 0
 *             -> zone 1
 *             -> zone 2
 *
 *
 * --------------------------------------------------------------------------
 * 1.4 Why iteration helpers are needed
 * --------------------------------------------------------------------------
 *
 * Many kernel places want to say:
 *
 *      "walk all online nodes"
 *      "walk all zones in all nodes"
 *
 * Rather than open-coding the traversal every time, Linux centralizes
 * the stepping logic here.
 *
 ******************************************************************************/

/******************************************************************************
 * 2. WHAT THIS FILE DOES IN ONE SENTENCE
 *
 * It provides helper functions to traverse:
 *
 *      online pgdats (NUMA nodes)
 * and
 *      zones across all online nodes
 *
 ******************************************************************************/

/******************************************************************************
 * 3. THE SOURCE
 ******************************************************************************/

/*
 * linux/mm/mmzone.c
 *
 * management codes for pgdats and zones.
 */

#include <linux/stddef.h>
#include <linux/mmzone.h>
#include <linux/module.h>

/******************************************************************************
 * 4. INCLUDE-LEVEL POINTERS
 *
 * --------------------------------------------------------------------------
 * 4.1 <linux/mmzone.h>
 * --------------------------------------------------------------------------
 *
 * This is the main header relevant to this file.
 *
 * It contains definitions/macros/types related to:
 *
 *      - struct zone
 *      - struct pglist_data
 *      - node iteration helpers/macros
 *      - zone iteration helpers/macros
 *
 *
 * --------------------------------------------------------------------------
 * 4.2 <linux/stddef.h>
 * --------------------------------------------------------------------------
 *
 * Basic definitions, NULL, offsetof-style helpers, etc.
 *
 *
 * --------------------------------------------------------------------------
 * 4.3 <linux/module.h>
 * --------------------------------------------------------------------------
 *
 * Common kernel module/export related definitions.
 *
 * Even tiny core files often include it.
 *
 ******************************************************************************/

/******************************************************************************
 * 5. IMPORTANT STRUCTURES / POINTERS TO KNOW BEFORE READING
 *
 * --------------------------------------------------------------------------
 * 5.1 struct pglist_data  (pgdat)
 * --------------------------------------------------------------------------
 *
 * Represents one NUMA node.
 *
 * Important mental fields:
 *
 *      pgdat->node_id
 *      pgdat->node_zones[MAX_NR_ZONES]
 *
 * So a pgdat is basically:
 *
 *      "this node, and its zones"
 *
 *
 * --------------------------------------------------------------------------
 * 5.2 struct zone
 * --------------------------------------------------------------------------
 *
 * Represents one memory zone.
 *
 * Important pointer:
 *
 *      zone->zone_pgdat
 *
 * That pointer tells you:
 *
 *      "which node does this zone belong to?"
 *
 *
 * --------------------------------------------------------------------------
 * 5.3 NODE_DATA(nid)
 * --------------------------------------------------------------------------
 *
 * Macro that returns the pgdat for a node id.
 *
 * Conceptually:
 *
 *      NODE_DATA(0) -> pgdat of node 0
 *
 *
 * --------------------------------------------------------------------------
 * 5.4 first_online_node / next_online_node()
 * --------------------------------------------------------------------------
 *
 * These are node-iteration helpers from elsewhere.
 *
 * This file builds pgdat-iteration on top of them.
 *
 *
 * --------------------------------------------------------------------------
 * 5.5 MAX_NR_ZONES
 * --------------------------------------------------------------------------
 *
 * Number of zones stored per node.
 *
 * Since zones are kept as an array:
 *
 *      pgdat->node_zones[0 ... MAX_NR_ZONES-1]
 *
 * next_zone() can move within that array using pointer arithmetic.
 *
 ******************************************************************************/

/******************************************************************************
 * 6. FUNCTION 1: first_online_pgdat()
 ******************************************************************************/

struct pglist_data *first_online_pgdat(void)
{
	return NODE_DATA(first_online_node);
}

/*
 * --------------------------------------------------------------------------
 * 6.1 What this means
 * --------------------------------------------------------------------------
 *
 * Give me the pgdat of the first online node.
 *
 *
 * --------------------------------------------------------------------------
 * 6.2 How it works
 * --------------------------------------------------------------------------
 *
 * Step 1:
 *      first_online_node
 *          -> global node id of first online node
 *
 * Step 2:
 *      NODE_DATA(first_online_node)
 *          -> convert node id into pgdat pointer
 *
 * Step 3:
 *      return pgdat pointer
 *
 *
 * --------------------------------------------------------------------------
 * 6.3 Why this helper exists
 * --------------------------------------------------------------------------
 *
 * A lot of code wants to start node iteration from:
 *
 *      first online node
 *
 * not from:
 *
 *      node 0 unconditionally
 *
 * because on some systems / configs / hotplug situations,
 * not all nodes are online.
 *
 *
 * --------------------------------------------------------------------------
 * 6.4 Example
 * --------------------------------------------------------------------------
 *
 * Suppose:
 *
 *      node ids present:   0 1 2 3
 *      online nodes:       0 2 3
 *
 * Then:
 *
 *      first_online_node = 0
 *      first_online_pgdat() = NODE_DATA(0)
 *
 *
 * Another example:
 *
 *      online nodes: 2 4 6
 *
 * Then:
 *
 *      first_online_node = 2
 *      first_online_pgdat() = NODE_DATA(2)
 *
 ******************************************************************************/

/******************************************************************************
 * 7. FUNCTION 2: next_online_pgdat()
 ******************************************************************************/

struct pglist_data *next_online_pgdat(struct pglist_data *pgdat)
{
	int nid = next_online_node(pgdat->node_id);

	if (nid == MAX_NUMNODES)
		return NULL;
	return NODE_DATA(nid);
}

/*
 * --------------------------------------------------------------------------
 * 7.1 What this means
 * --------------------------------------------------------------------------
 *
 * Given current node's pgdat, return the next online node's pgdat.
 *
 *
 * --------------------------------------------------------------------------
 * 7.2 Input
 * --------------------------------------------------------------------------
 *
 *      pgdat
 *          current node descriptor
 *
 *
 * --------------------------------------------------------------------------
 * 7.3 Flow
 * --------------------------------------------------------------------------
 *
 *      current pgdat
 *          -> read pgdat->node_id
 *          -> call next_online_node(current_id)
 *          -> get next node id
 *
 *      if next node id is MAX_NUMNODES
 *          -> no more online nodes
 *          -> return NULL
 *
 *      else
 *          -> return NODE_DATA(next node id)
 *
 *
 * --------------------------------------------------------------------------
 * 7.4 Why MAX_NUMNODES check exists
 * --------------------------------------------------------------------------
 *
 * next_online_node() returns sentinel/end marker when no more online nodes
 * exist.
 *
 * That sentinel is:
 *
 *      MAX_NUMNODES
 *
 * So:
 *
 *      nid == MAX_NUMNODES
 *          means end of iteration
 *
 *
 * --------------------------------------------------------------------------
 * 7.5 Example
 * --------------------------------------------------------------------------
 *
 * Online nodes:
 *
 *      0 -> 2 -> 5
 *
 * Then:
 *
 *      next_online_pgdat(pgdat(0)) -> pgdat(2)
 *      next_online_pgdat(pgdat(2)) -> pgdat(5)
 *      next_online_pgdat(pgdat(5)) -> NULL
 *
 *
 * --------------------------------------------------------------------------
 * 7.6 Big idea
 * --------------------------------------------------------------------------
 *
 * This converts:
 *
 *      node-id iteration
 *
 * into:
 *
 *      pgdat-pointer iteration
 *
 ******************************************************************************/

/******************************************************************************
 * 8. COMMENT ADDED IN YOUR VERSION
 *
 *      /*XXX: Next zone after the given * /
 *
 * This is your explanatory note, and it is accurate in spirit:
 *
 *      next_zone() returns the next zone after the current one
 *
 * But more precisely:
 *
 *      it returns the next zone in a global traversal across all zones
 *      of all online nodes
 *
 ******************************************************************************/

/******************************************************************************
 * 9. FUNCTION 3: next_zone()
 ******************************************************************************/

/*
 * next_zone - helper magic for for_each_zone()
 */
struct zone *next_zone(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		zone++;
	else {
		pgdat = next_online_pgdat(pgdat);
		if (pgdat)
			zone = pgdat->node_zones;
		else
			zone = NULL;
	}
	return zone;
}

/******************************************************************************
 * 10. DETAILED EXPLANATION OF next_zone()
 *
 * This is the real core of the file.
 *
 * It provides zone iteration across the whole system.
 *
 * IMPORTANT:
 *      It does not just move to the next zone in the same node.
 *      It also jumps from the last zone of one node
 *      to the first zone of the next online node.
 *
 ******************************************************************************/

/******************************************************************************
 * 11. HOW next_zone() THINKS
 *
 * It receives:
 *
 *      current zone pointer
 *
 * Then it asks:
 *
 *      "am I still inside the current node's zone array?"
 *
 * If yes:
 *      just increment zone pointer
 *
 * If no (i.e. current zone is last zone in the node):
 *      move to next online node
 *      return first zone of that next node
 *
 * If no next online node exists:
 *      return NULL
 *
 ******************************************************************************/

/******************************************************************************
 * 12. STEP-BY-STEP WALKTHROUGH OF next_zone()
 *
 * --------------------------------------------------------------------------
 * 12.1 Get the parent node of current zone
 * --------------------------------------------------------------------------
 *
 *      pg_data_t *pgdat = zone->zone_pgdat;
 *
 * Since every zone belongs to some node, this pointer tells us:
 *
 *      "which node's zone array am I in?"
 *
 *
 * --------------------------------------------------------------------------
 * 12.2 Check if current zone is NOT the last zone in that node
 * --------------------------------------------------------------------------
 *
 *      if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
 *
 * Let's decode this:
 *
 *      pgdat->node_zones
 *          = pointer to first zone in this node
 *
 *      pgdat->node_zones + MAX_NR_ZONES - 1
 *          = pointer to last slot in this node's zone array
 *
 * So condition means:
 *
 *      "is current zone before the last zone slot?"
 *
 * If yes:
 *
 *      zone++;
 *
 * which means:
 *
 *      move to next zone array entry
 *
 *
 * --------------------------------------------------------------------------
 * 12.3 Else: current zone is the last zone in this node
 * --------------------------------------------------------------------------
 *
 *      else {
 *          pgdat = next_online_pgdat(pgdat);
 *          if (pgdat)
 *              zone = pgdat->node_zones;
 *          else
 *              zone = NULL;
 *      }
 *
 * Here logic is:
 *
 *      - current node exhausted
 *      - move to next online node
 *      - if next node exists:
 *              return its first zone
 *      - else:
 *              iteration done -> NULL
 *
 ******************************************************************************/

/******************************************************************************
 * 13. POINTER ARITHMETIC VIEW
 *
 * Suppose:
 *
 *      pgdat->node_zones = &zones[0]
 *      MAX_NR_ZONES = 3
 *
 * Then:
 *
 *      pgdat->node_zones + 0  -> zone 0
 *      pgdat->node_zones + 1  -> zone 1
 *      pgdat->node_zones + 2  -> zone 2 (last)
 *
 * So code:
 *
 *      if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
 *
 * means:
 *
 *      if current zone pointer is before zone 2
 *
 * then:
 *
 *      zone++
 *
 ******************************************************************************/

/******************************************************************************
 * 14. COMPLETE FLOW EXAMPLE
 *
 * Assume:
 *
 *      MAX_NR_ZONES = 3
 *
 * Online nodes:
 *
 *      Node 0
 *      Node 2
 *
 * Memory layout:
 *
 *      Node 0:
 *          node_zones[0] = Z0
 *          node_zones[1] = Z1
 *          node_zones[2] = Z2
 *
 *      Node 2:
 *          node_zones[0] = Z0
 *          node_zones[1] = Z1
 *          node_zones[2] = Z2
 *
 *
 * Iteration:
 *
 *      start = Node0.Zone0
 *
 *      next_zone(Node0.Zone0)
 *          -> same node, not last
 *          -> Node0.Zone1
 *
 *      next_zone(Node0.Zone1)
 *          -> same node, not last
 *          -> Node0.Zone2
 *
 *      next_zone(Node0.Zone2)
 *          -> last zone in node
 *          -> next_online_pgdat(Node0)
 *          -> Node2
 *          -> Node2.Zone0
 *
 *      next_zone(Node2.Zone0)
 *          -> Node2.Zone1
 *
 *      next_zone(Node2.Zone1)
 *          -> Node2.Zone2
 *
 *      next_zone(Node2.Zone2)
 *          -> last zone in node
 *          -> next_online_pgdat(Node2) = NULL
 *          -> return NULL
 *
 ******************************************************************************/

/******************************************************************************
 * 15. WHAT "for_each_zone()" REALLY NEEDS
 *
 * The comment says:
 *
 *      helper magic for for_each_zone()
 *
 * Meaning:
 *      this function is typically not called manually everywhere.
 *      It is used by iteration macros.
 *
 * Conceptually, a macro may behave like:
 *
 *      zone = first zone of first online node;
 *      while (zone != NULL) {
 *          ...
 *          zone = next_zone(zone);
 *      }
 *
 * So next_zone() is the step-function of that traversal.
 *
 ******************************************************************************/

/******************************************************************************
 * 16. IMPORTANT CONCEPTUAL DISTINCTION
 *
 * --------------------------------------------------------------------------
 * 16.1 first_online_pgdat / next_online_pgdat
 * --------------------------------------------------------------------------
 *
 * These operate at:
 *
 *      NODE LEVEL
 *
 *
 * --------------------------------------------------------------------------
 * 16.2 next_zone
 * --------------------------------------------------------------------------
 *
 * This operates at:
 *
 *      ZONE LEVEL
 *
 * but it knows how to cross node boundaries.
 *
 ******************************************************************************/

/******************************************************************************
 * 17. WHAT THIS FILE DOES NOT CHECK
 *
 * This is important.
 *
 * next_zone() does NOT check whether a zone:
 *
 *      - has managed pages
 *      - has present pages
 *      - is empty
 *      - is usable for current allocation class
 *      - matches GFP constraints
 *
 * It only performs structural traversal.
 *
 * The caller decides whether zone is meaningful for its purpose.
 *
 ******************************************************************************/

/******************************************************************************
 * 18. WHY THIS FILE IS SO SMALL
 *
 * Because the heavy logic is elsewhere.
 *
 * This file is intentionally tiny because its job is just:
 *
 *      "How do I step from one memory topology object to the next?"
 *
 * That's it.
 *
 * Small file, but heavily reused.
 *
 ******************************************************************************/

/******************************************************************************
 * 19. WHERE THESE HELPERS BECOME IMPORTANT
 *
 * You should mentally connect this file with:
 *
 *      mm/page_alloc.c
 *          → allocator scans zones
 *
 *      mm/vmscan.c
 *          → reclaim scans zones
 *
 *      boot / init memory setup
 *          → node and zone initialization
 *
 *      statistics / /proc / VM counters
 *          → aggregate info over zones
 *
 * This file is infrastructure for that iteration.
 *
 ******************************************************************************/

/******************************************************************************
 * 20. MENTAL ASCII MODEL
 *
 * --------------------------------------------------------------------------
 * 20.1 Physical hierarchy
 * --------------------------------------------------------------------------
 *
 *      [System]
 *         |
 *         +-- [pgdat node0]
 *         |       +-- zone0
 *         |       +-- zone1
 *         |       +-- zone2
 *         |
 *         +-- [pgdat node1]
 *         |       +-- zone0
 *         |       +-- zone1
 *         |       +-- zone2
 *         |
 *         +-- [pgdat node2]
 *                 +-- zone0
 *                 +-- zone1
 *                 +-- zone2
 *
 *
 * --------------------------------------------------------------------------
 * 20.2 Flattened traversal produced by next_zone()
 * --------------------------------------------------------------------------
 *
 *      node0.zone0
 *         ->
 *      node0.zone1
 *         ->
 *      node0.zone2
 *         ->
 *      node1.zone0
 *         ->
 *      node1.zone1
 *         ->
 *      node1.zone2
 *         ->
 *      node2.zone0
 *         ->
 *      ...
 *         ->
 *      NULL
 *
 ******************************************************************************/

/******************************************************************************
 * 21. INTERVIEW / READING TAKEAWAYS
 *
 * If someone asks:
 *
 *      "What does mmzone.c do?"
 *
 * Good answer:
 *
 *      "It provides basic topology traversal helpers for Linux memory
 *       management. It lets the kernel iterate across online NUMA nodes
 *       and across zones inside those nodes. The important helper is
 *       next_zone(), which walks a flattened sequence of zones across all
 *       online nodes."
 *
 *
 * If someone asks:
 *
 *      "Does it manage zones?"
 *
 * Answer:
 *
 *      "No, not really. It does not implement allocation or reclaim.
 *       It only provides the stepping logic for traversing pgdats and zones."
 *
 ******************************************************************************/

/******************************************************************************
 * 22. FUNCTION-BY-FUNCTION MINI SUMMARY
 *
 * first_online_pgdat()
 *      → return pgdat of first online node
 *
 * next_online_pgdat(pgdat)
 *      → return pgdat of next online node, or NULL
 *
 * next_zone(zone)
 *      → return next zone in same node if possible,
 *        otherwise jump to first zone of next online node,
 *        otherwise NULL
 *
 ******************************************************************************/

/******************************************************************************
 * 23. FINAL BIG-PICTURE SUMMARY
 *
 * linux-2.6.20/mm/mmzone.c is a topology traversal helper file.
 *
 * It understands two relationships:
 *
 *      zone  -> belongs to pgdat
 *      pgdat -> has an array of zones
 *
 * Using that, it provides a clean way to iterate through:
 *
 *      all online nodes
 *      all zones across all online nodes
 *
 * That is why this file is small but foundational.
 *
 ******************************************************************************/
