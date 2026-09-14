#ifndef IAP_METADATA_H
#define IAP_METADATA_H

#include "iap_storage.h"

void iap_metadata_default(IapMetaRecord *record);
IapError iap_metadata_load(IapMetaRecord *record);
IapError iap_metadata_store(IapMetaRecord *record);
IapError iap_metadata_confirm_running(void);

#endif
