/**
 * @file test_event_manager.c
 * @brief Event Manager Component Unit Tests
 * 
 * @author robOS Team
 * @date 2025
 */

#include "unity.h"
#include "event_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "TEST_EVENT_MANAGER";

// Test event base
ESP_EVENT_DEFINE_BASE(TEST_EVENTS);

// Test event IDs
enum {
    TEST_EVENT_1,
    TEST_EVENT_2,
    TEST_EVENT_WITH_DATA,
};

// Test data structure
typedef struct {
    int value;
    char message[32];
} test_event_data_t;

// Test state
static struct {
    int event_received_count;
    int last_event_id;
    test_event_data_t last_event_data;
    SemaphoreHandle_t event_received_sem;
} test_state;

/**
 * @brief Test event handler
 */
static void test_event_handler(void *handler_args, esp_event_base_t event_base, 
                              int32_t event_id, void *event_data)
{
    test_state.event_received_count++;
    test_state.last_event_id = event_id;
    
    if (event_data && event_id == TEST_EVENT_WITH_DATA) {
        test_state.last_event_data = *(test_event_data_t *)event_data;
    }
    
    if (test_state.event_received_sem) {
        xSemaphoreGive(test_state.event_received_sem);
    }
    
    ESP_LOGI(TAG, "Test event handler called - ID: %" PRId32, event_id);
}

/**
 * @brief Setup function called before each test
 */
void setUp(void)
{
    // Reset test state
    memset(&test_state, 0, sizeof(test_state));
    test_state.event_received_sem = xSemaphoreCreateCounting(10, 0);  // Max 10 events, initial count 0
    TEST_ASSERT_NOT_NULL(test_state.event_received_sem);
}

/**
 * @brief Teardown function called after each test
 */
void tearDown(void)
{
    if (test_state.event_received_sem) {
        vSemaphoreDelete(test_state.event_received_sem);
        test_state.event_received_sem = NULL;
    }
    
    // Ensure event manager is deinitialized
    if (event_manager_is_initialized()) {
        event_manager_deinit();
    }
}

/**
 * @brief Test event manager initialization with default config
 */
void test_event_manager_init_default_config(void)
{
    ESP_LOGI(TAG, "Testing event manager initialization with default config");
    
    // Should not be initialized initially
    TEST_ASSERT_FALSE(event_manager_is_initialized());
    TEST_ASSERT_FALSE(event_manager_is_running());
    
    // Initialize with default config
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Should be initialized but not running
    TEST_ASSERT_TRUE(event_manager_is_initialized());
    TEST_ASSERT_FALSE(event_manager_is_running());
    
    // Get status
    event_manager_status_t status;
    ret = event_manager_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(status.initialized);
    TEST_ASSERT_FALSE(status.running);
    TEST_ASSERT_EQUAL(0, status.total_events_sent);
    TEST_ASSERT_EQUAL(0, status.total_events_received);
    TEST_ASSERT_EQUAL(0, status.active_handlers);
}

/**
 * @brief Test event manager initialization with custom config
 */
void test_event_manager_init_custom_config(void)
{
    ESP_LOGI(TAG, "Testing event manager initialization with custom config");
    
    event_manager_config_t config = {
        .event_queue_size = 64,
        .event_task_stack_size = 8192,
        .event_task_priority = 10,
        .enable_statistics = true,
        .enable_logging = true
    };
    
    esp_err_t ret = event_manager_init(&config);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    TEST_ASSERT_TRUE(event_manager_is_initialized());
    
    // Try to initialize again (should fail)
    ret = event_manager_init(&config);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * @brief Test event manager start and stop
 */
void test_event_manager_start_stop(void)
{
    ESP_LOGI(TAG, "Testing event manager start/stop");
    
    // Initialize first
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start
    ret = event_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_TRUE(event_manager_is_running());
    
    // Start again (should be OK)
    ret = event_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Stop
    ret = event_manager_stop();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_FALSE(event_manager_is_running());
}

/**
 * @brief Test event handler registration and unregistration
 */
void test_event_manager_handler_registration(void)
{
    ESP_LOGI(TAG, "Testing event handler registration");
    
    // Initialize and start
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = event_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Register handler
    ret = event_manager_register_handler(TEST_EVENTS, TEST_EVENT_1, 
                                        test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check status
    event_manager_status_t status;
    ret = event_manager_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(1, status.active_handlers);
    
    // Unregister handler
    ret = event_manager_unregister_handler(TEST_EVENTS, TEST_EVENT_1, 
                                          test_event_handler);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Check status again
    ret = event_manager_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL(0, status.active_handlers);
}

/**
 * @brief Test event posting and handling
 */
void test_event_manager_post_and_handle(void)
{
    ESP_LOGI(TAG, "Testing event posting and handling");
    
    // Initialize and start
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = event_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Register handler
    ret = event_manager_register_handler(TEST_EVENTS, TEST_EVENT_1, 
                                        test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Post event
    ret = event_manager_post_event(TEST_EVENTS, TEST_EVENT_1, NULL, 0, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Wait for event to be handled
    BaseType_t sem_ret = xSemaphoreTake(test_state.event_received_sem, pdMS_TO_TICKS(1000));
    TEST_ASSERT_EQUAL(pdTRUE, sem_ret);
    
    // Check that event was received
    TEST_ASSERT_EQUAL(1, test_state.event_received_count);
    TEST_ASSERT_EQUAL(TEST_EVENT_1, test_state.last_event_id);
    
    // Check status
    event_manager_status_t status;
    ret = event_manager_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_GREATER_THAN(0, status.total_events_sent);
    TEST_ASSERT_GREATER_THAN(0, status.total_events_received);
}

/**
 * @brief Test event posting with data
 */
void test_event_manager_post_with_data(void)
{
    ESP_LOGI(TAG, "Testing event posting with data");
    
    // Initialize and start
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = event_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Register handler
    ret = event_manager_register_handler(TEST_EVENTS, TEST_EVENT_WITH_DATA, 
                                        test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Prepare test data
    test_event_data_t test_data = {
        .value = 42,
    };
    strcpy(test_data.message, "Hello, robOS!");
    
    // Post event with data
    ret = event_manager_post_event(TEST_EVENTS, TEST_EVENT_WITH_DATA, 
                                  &test_data, sizeof(test_data), 1000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Wait for event to be handled
    BaseType_t sem_ret = xSemaphoreTake(test_state.event_received_sem, pdMS_TO_TICKS(1000));
    TEST_ASSERT_EQUAL(pdTRUE, sem_ret);
    
    // Check that event was received with correct data
    TEST_ASSERT_EQUAL(1, test_state.event_received_count);
    TEST_ASSERT_EQUAL(TEST_EVENT_WITH_DATA, test_state.last_event_id);
    TEST_ASSERT_EQUAL(42, test_state.last_event_data.value);
    TEST_ASSERT_EQUAL_STRING("Hello, robOS!", test_state.last_event_data.message);
}

/**
 * @brief Test multiple events and handlers
 */
void test_event_manager_multiple_events(void)
{
    ESP_LOGI(TAG, "Testing multiple events and handlers");
    
    // Initialize and start
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    ret = event_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Register handlers for different events
    ret = event_manager_register_handler(TEST_EVENTS, TEST_EVENT_1, 
                                        test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = event_manager_register_handler(TEST_EVENTS, TEST_EVENT_2, 
                                        test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Post multiple events
    ret = event_manager_post_event(TEST_EVENTS, TEST_EVENT_1, NULL, 0, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = event_manager_post_event(TEST_EVENTS, TEST_EVENT_2, NULL, 0, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    ret = event_manager_post_event(TEST_EVENTS, TEST_EVENT_1, NULL, 0, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Wait for all events to be handled
    for (int i = 0; i < 3; i++) {
        BaseType_t sem_ret = xSemaphoreTake(test_state.event_received_sem, pdMS_TO_TICKS(1000));
        TEST_ASSERT_EQUAL(pdTRUE, sem_ret);
    }
    
    // Check that all events were received
    TEST_ASSERT_EQUAL(3, test_state.event_received_count);
}

/**
 * @brief Test error conditions
 */
void test_event_manager_error_conditions(void)
{
    ESP_LOGI(TAG, "Testing error conditions");
    
    // Test operations without initialization
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, event_manager_start());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, event_manager_stop());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_manager_register_handler(TEST_EVENTS, TEST_EVENT_1, NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, event_manager_post_event(TEST_EVENTS, TEST_EVENT_1, NULL, 0, 1000));
    
    // Test with NULL arguments
    event_manager_status_t *null_status = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_manager_get_status(null_status));
    
    // Initialize for remaining tests
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Test invalid handler registration
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, event_manager_register_handler(TEST_EVENTS, TEST_EVENT_1, NULL, NULL));
}

/**
 * @brief Test deinitialization
 */
void test_event_manager_deinit(void)
{
    ESP_LOGI(TAG, "Testing event manager deinitialization");
    
    // Initialize first
    esp_err_t ret = event_manager_init(NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Start
    ret = event_manager_start();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Register a handler
    ret = event_manager_register_handler(TEST_EVENTS, TEST_EVENT_1, 
                                        test_event_handler, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Deinitialize
    ret = event_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    
    // Should not be initialized anymore
    TEST_ASSERT_FALSE(event_manager_is_initialized());
    TEST_ASSERT_FALSE(event_manager_is_running());
    
    // Deinitialize again (should be OK)
    ret = event_manager_deinit();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, ret);
}

/**
 * @brief Run all tests
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting Event Manager Unit Tests");
    
    UNITY_BEGIN();
    
    RUN_TEST(test_event_manager_init_default_config);
    RUN_TEST(test_event_manager_init_custom_config);
    RUN_TEST(test_event_manager_start_stop);
    RUN_TEST(test_event_manager_handler_registration);
    RUN_TEST(test_event_manager_post_and_handle);
    RUN_TEST(test_event_manager_post_with_data);
    RUN_TEST(test_event_manager_multiple_events);
    RUN_TEST(test_event_manager_error_conditions);
    RUN_TEST(test_event_manager_deinit);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "Event Manager Unit Tests Completed");
}