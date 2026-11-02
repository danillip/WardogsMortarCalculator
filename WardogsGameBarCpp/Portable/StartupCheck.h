#pragma once
#include <windows.h>
namespace wardogs {
// Read-only prerequisites probe. External pages open only after a button click.
// Returns false if the user closes/cancels the startup dialog.
bool ConfirmPortableStartup(HINSTANCE instance,bool english);
}
