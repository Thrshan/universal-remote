#include "bt_output.h"
#include <string.h>

bool bt_output_tap(const bt_output_t *output, const bt_key_report_t *report)
{
    if (!output->active(output->context)) return false;
    bool pressed = output->send(output->context, report);
    output->pause(output->context, 40);
    bt_key_report_t release = *report;
    memset(release.data, 0, sizeof(release.data));
    bool released = output->send(output->context, &release);
    output->pause(output->context, 10);
    return pressed && released && output->active(output->context);
}

bool bt_output_text(const bt_output_t *output, const char *text)
{
    if (!bt_text_valid(text) || !text[0]) return false;
    for (unsigned i = 0; text[i]; ++i) {
        bt_key_report_t report;
        if (!bt_text_report(text[i], &report) || !bt_output_tap(output, &report)) return false;
    }
    return true;
}
