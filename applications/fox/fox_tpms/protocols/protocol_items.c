#include "protocol_items.h"

const SubGhzProtocol* tpms_protocol_registry_items[] = {
    &tpms_protocol_schrader_gg4,
    &tpms_protocol_ford,
    &tpms_protocol_renault,
    &tpms_protocol_citroen,
    &tpms_protocol_pmv107j,
    &tpms_protocol_abarth_124,
    &tpms_protocol_schrader_eg53ma4,
    &tpms_protocol_schrader_smd3ma4,
};

const SubGhzProtocolRegistry tpms_protocol_registry = {
    .items = tpms_protocol_registry_items,
    .size = COUNT_OF(tpms_protocol_registry_items)};
