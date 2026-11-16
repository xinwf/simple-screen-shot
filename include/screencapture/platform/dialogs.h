#pragma once
#include <string>

namespace sc::platform {

// Modal GTK info dialog with an extra check button under the message.
// Blocks until OK is clicked; returns whether the checkbox was checked.
bool showInfoDialogWithCheckbox(const std::string& title, const std::string& message,
                                const std::string& checkboxText);

} // namespace sc::platform
