extern "C" {

#include "sai.h"
#include "saistatus.h"
#include "saiextensions.h"
}

#include <string.h>

#include <map>
#include <logger.h>
#include <sairedis.h>

#include "ocshelper.h"

using namespace swss;


/* Initialize all ocs api pointers */
extern sai_switch_api_t                 *sai_switch_api;
extern sai_router_interface_api_t       *sai_router_intfs_api;
sai_ocs_cross_connect_api_t             *sai_ocs_cross_connect_api;
sai_ocs_port_api_t                      *sai_ocs_port_api;


extern const char *test_profile_get_value (
    _In_ sai_switch_profile_id_t profile_id,
    _In_ const char *variable);

extern int test_profile_get_next_value (
    _In_ sai_switch_profile_id_t profile_id,
    _Out_ const char **variable,
    _Out_ const char **value);


void initOcsApi()
{
    SWSS_LOG_ENTER();
    SWSS_LOG_NOTICE("Initializing OCS API");

    sai_service_method_table_t services = {
        test_profile_get_value,
        test_profile_get_next_value
    };

    sai_api_initialize(0, (const sai_service_method_table_t *)&services);

    sai_api_query(SAI_API_SWITCH, (void **)&sai_switch_api);
    sai_api_query(SAI_API_ROUTER_INTERFACE, (void **)&sai_router_intfs_api);
    sai_api_query((sai_api_t)SAI_API_OCS_CROSS_CONNECT, (void **)&sai_ocs_cross_connect_api);
    sai_api_query((sai_api_t)SAI_API_OCS_PORT, (void **)&sai_ocs_port_api);

    sai_log_set(SAI_API_SWITCH,                                 SAI_LOG_LEVEL_NOTICE);
    sai_log_set(SAI_API_ROUTER_INTERFACE,                       SAI_LOG_LEVEL_NOTICE);
    sai_log_set((sai_api_t)SAI_API_OCS_CROSS_CONNECT,           SAI_LOG_LEVEL_NOTICE);
    sai_log_set((sai_api_t)SAI_API_OCS_PORT,                    SAI_LOG_LEVEL_NOTICE);
}
