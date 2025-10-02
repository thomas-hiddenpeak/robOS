idf_component_register(
    SRCS "test_arp_config.c"
    REQUIRES lwip
    INCLUDE_DIRS "."
)

# Override LWIP compile flags to fix ARP table size and management
target_compile_definitions(${COMPONENT_LIB} INTERFACE
    ARP_TABLE_SIZE=200
    ARP_MAXAGE=300
    ETHARP_TABLE_MATCH_NETIF=1
    ARP_QUEUEING=1
    ARP_QUEUE_LEN=20
    ETHARP_TRUST_IP_MAC=1
    ETHARP_SUPPORT_STATIC_ENTRIES=1
    MEMP_NUM_ARP_QUEUE=30
)