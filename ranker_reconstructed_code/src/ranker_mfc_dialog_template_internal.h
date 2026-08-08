#pragma once

#include "ranker_mfc_runtime.h"

namespace ranker {

#ifdef _WIN32
DWORD MfcDialogTemplateStyle(const DLGTEMPLATE* dialog_template);
SIZE MfcDialogTemplateDialogUnits(const DLGTEMPLATE* dialog_template);
#endif

} // namespace ranker
