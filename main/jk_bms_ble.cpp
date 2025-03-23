#include "jk_bms_ble.h"

//
static void ble_connect(void *disc);

static int blecent_gap_event(struct ble_gap_event *event, void *arg)
{
  // struct ble_gap_conn_desc desc;
  struct ble_hs_adv_fields fields;
  int rc;
  printf("Event: %d\n", event->type);

  switch (event->type) {
  case BLE_GAP_EVENT_DISC:
    rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
    if (rc != 0) return 0;

    // Print mac address
    ESP_LOGI(tag, "Device Address: %02x:%02x:%02x:%02x:%02x:%02x", event->disc.addr.val[5], event->disc.addr.val[4], event->disc.addr.val[3], event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0]);
    
    ble_connect(&event->disc);
    break;

  default:
    break;
  }

  return 0;
}

/**
 * Determine if the device is the target device
 */
static int is_target_device(const struct ble_gap_disc_desc *disc, const std::string& peer_addr_str)
{
    struct ble_hs_adv_fields fields;
    int rc;

    /* The device has to be advertising connectability. */
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND && disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) {
        return 0;
    }

    uint8_t peer_addr[6] = {0};

    // Parse MAC address in reverse byte order (BLE style)
    int parsed = sscanf(peer_addr_str.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                        &peer_addr[5], &peer_addr[4], &peer_addr[3],
                        &peer_addr[2], &peer_addr[1], &peer_addr[0]);

    if (parsed != 6) {
        ESP_LOGE("BLE", "Invalid address format: %s", peer_addr_str.c_str());
        return 0;
    }

    // Compare parsed address with discovered device
    if (memcmp(peer_addr, disc->addr.val, sizeof(disc->addr.val)) != 0) {
      return 0;
    }

    return 1;
}

/**
 * BLE connect
 */
static void ble_connect(void *disc)
{
    uint8_t own_addr_type;
    int rc;
    ble_addr_t *addr;

    /* Don't do anything if we don't care about this advertiser. */
    if (!is_target_device((struct ble_gap_disc_desc *)disc, "c8:47:80:1f:05:b6")) {
        return;
    }

    /* Scanning must be stopped before a connection can be initiated. */
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        ESP_LOGE(tag, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    /* Figure out address to use for connect (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(tag, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Try to connect the the advertiser.  Allow 30 seconds (30000 ms) for
     * timeout.
     */
    addr = &((struct ble_gap_disc_desc *)disc)->addr;

    rc = ble_gap_connect(own_addr_type, addr, 30000, NULL,
                         blecent_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(tag, "Error: Failed to connect to device; addr_type=%d; rc=%d\n", addr->type, rc);
        return;
    }
}

void ble_scan(){
  uint8_t own_addr_type;
  struct ble_gap_disc_params disc_params;
  int rc;

  /* Figure out address to use while advertising (no privacy for now) */
  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
      MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
      return;
  }

  /* Tell the controller to filter duplicates; we don't want to process
   * repeated advertisements from the same device.
   */
  disc_params.filter_duplicates = 1;

  /**
   * Perform a passive scan.  I.e., don't send follow-up scan requests to
   * each advertiser.
   */
  disc_params.passive = 1;

  /* Use defaults for the rest of the parameters. */
  disc_params.itvl = 0;
  disc_params.window = 0;
  disc_params.filter_policy = 0;
  disc_params.limited = 0;

  rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                    blecent_gap_event, NULL);
  if (rc != 0) {
      MODLOG_DFLT(ERROR, "Error initiating GAP discovery procedure; rc=%d\n",
                  rc);
  }
}

static void blecent_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void blecent_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

#if !CONFIG_EXAMPLE_INIT_DEINIT_LOOP
    /* Begin scanning for a peripheral to connect to. */
    ble_scan();
#endif
}

void blecent_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

JkBmsBle::JkBmsBle() {
  init_ble();
  // TODO Auto-generated constructor stub
}

JkBmsBle::~JkBmsBle() {
  // TODO Auto-generated destructor stub
}

void JkBmsBle::init_ble() {
  int rc;
    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if  (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    /* Configure the host. */
    ble_hs_cfg.reset_cb = blecent_on_reset;
    ble_hs_cfg.sync_cb = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("esp32");
    assert(rc == 0);

    nimble_port_freertos_init(blecent_host_task);
}

