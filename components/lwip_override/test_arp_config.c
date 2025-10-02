/**
 * @file test_arp_config.c
 * @brief Test program to verify ARP table configuration
 */

#include "lwip/etharp.h"
#include "lwip/opt.h"
#include <stdio.h>

void test_arp_config(void) {
  printf("=== ARP Configuration Test ===\n");
  printf("ARP_TABLE_SIZE: %d\n", ARP_TABLE_SIZE);
  printf("ARP_MAXAGE: %d\n", ARP_MAXAGE);
  printf("ARP_QUEUEING: %d\n", ARP_QUEUEING);
  printf("ARP_QUEUE_LEN: %d\n", ARP_QUEUE_LEN);
  printf("ETHARP_SUPPORT_STATIC_ENTRIES: %d\n", ETHARP_SUPPORT_STATIC_ENTRIES);
#ifdef ETHARP_TRUST_IP_MAC
  printf("ETHARP_TRUST_IP_MAC: %d\n", ETHARP_TRUST_IP_MAC);
#else
  printf("ETHARP_TRUST_IP_MAC: not defined\n");
#endif
  printf("ETHARP_TABLE_MATCH_NETIF: %d\n", ETHARP_TABLE_MATCH_NETIF);
  printf("MEMP_NUM_ARP_QUEUE: %d\n", MEMP_NUM_ARP_QUEUE);
  printf("==============================\n");
}