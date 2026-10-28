#include "bt_mouse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, count, fail_at;
static bt_key_report_t reports[8192];
#define CHECK(value) do { ++checks; if (!(value)) { fprintf(stderr, "Mouse line %d: %s\n", __LINE__, #value); abort(); } } while (0)
static bool active(void *context) { return true; }
static bool send(void *context, const bt_key_report_t *report)
{
    CHECK(report->report_id == BT_MOUSE_REPORT_ID && report->length == BT_MOUSE_REPORT_SIZE);
    CHECK(count < 8192);
    reports[count++] = *report;
    return count != fail_at;
}
static void pause_output(void *context, unsigned milliseconds) { CHECK(milliseconds == 40); }
static const bt_output_t output = {.active = active, .send = send, .pause = pause_output};
static remote_t remote = {.transport = REMOTE_BT, .bt = {.kind = BT_REMOTE_MOUSE}};
static bt_mouse_t mouse;
static void input(unsigned key, bt_mouse_input_t action, uint32_t time)
{ CHECK(bt_mouse_input(&mouse, &remote, key, action, time, &output)); }
static void tap(unsigned key, uint32_t time)
{ input(key, BT_MOUSE_PRESS, time); input(key, BT_MOUSE_RELEASE, time + 100); }
static void tick(uint32_t time) { CHECK(bt_mouse_tick(&mouse, &remote, time, &output)); }

int main(void)
{
    bt_mouse_reset(&mouse, 123);
    CHECK(bt_mouse_speed(&remote) == 5);
    CHECK(!(bt_mouse_mask(0) & (1u << (REMOTE_KEY_MODE - 1))));
    tap(REMOTE_KEY_OK, 0); tap(REMOTE_KEY_OK, 200);
    CHECK(count == 4 && reports[0].data[0] == 1 && reports[1].data[0] == 0
        && reports[2].data[0] == 1 && reports[3].data[0] == 0); // Double click.
    input(REMOTE_KEY_OK, BT_MOUSE_PRESS, 400);
    CHECK(count == 4); // A hold must not first emit a click.
    input(REMOTE_KEY_OK, BT_MOUSE_HOLD, 1100);
    input(REMOTE_KEY_OK, BT_MOUSE_RELEASE, 1200);
    CHECK(mouse.buttons == 1 && count == 5);
    input(REMOTE_KEY_RIGHT, BT_MOUSE_PRESS, 1300); tick(1300); tick(1320);
    CHECK(reports[count - 1].data[0] == 1 && (int8_t)reports[count - 1].data[1] == 10);
    unsigned before = count;
    tick(1321); CHECK(count == before);
    input(REMOTE_KEY_RIGHT, BT_MOUSE_RELEASE, 1340); tick(1360); CHECK(count == before);
    tap(REMOTE_KEY_PLAY_PAUSE, 1400);
    CHECK(reports[count - 2].data[0] == 3 && reports[count - 1].data[0] == 1); // Preserve drag.
    tap(REMOTE_KEY_VOL_CLICK, 1600); CHECK(reports[count - 2].data[0] == 5 && mouse.buttons == 1);
    tap(REMOTE_KEY_CUSTOM2, 1800); CHECK(reports[count - 2].data[0] == 9);
    tap(REMOTE_KEY_CUSTOM3, 2000); CHECK(reports[count - 2].data[0] == 17);
    tap(REMOTE_KEY_VOL_UP, 2200); CHECK(reports[count - 1].data[3] == 1 && mouse.buttons == 1);
    tap(REMOTE_KEY_VOL_DOWN, 2400); CHECK((int8_t)reports[count - 1].data[3] == -1);
    tap(REMOTE_KEY_CH_UP, 2600); CHECK(reports[count - 1].data[4] == 1);
    tap(REMOTE_KEY_CH_DOWN, 2800); CHECK((int8_t)reports[count - 1].data[4] == -1);
    tap(REMOTE_KEY_OK, 3000); CHECK(!mouse.buttons && !reports[count - 1].data[0]); // Drop without extra click.
    before = count;
    input(REMOTE_KEY_OK, BT_MOUSE_RELEASE, 3200); CHECK(count == before); // Unmatched release.

    remote.bt.mouse_mode = BT_MOUSE_PATTERNS;
    tap(REMOTE_KEY_RIGHT, 4000); tick(4120);
    CHECK(mouse.movement == REMOTE_KEY_RIGHT && reports[count - 1].data[1] == 10);
    input(REMOTE_KEY_RIGHT, BT_MOUSE_REPEAT, 4200); CHECK(mouse.movement == REMOTE_KEY_RIGHT);
    tap(REMOTE_KEY_RIGHT, 4300); CHECK(!mouse.movement);
    tap(REMOTE_KEY_LEFT, 4500); tick(4600); CHECK((int8_t)reports[count - 1].data[1] == -10);
    tap(REMOTE_KEY_UP, 4700); tick(4800);
    CHECK(mouse.movement == REMOTE_KEY_UP && !reports[count - 1].data[1] && (int8_t)reports[count - 1].data[2] == -10);
    input(REMOTE_KEY_UP, BT_MOUSE_PRESS, 4900); input(REMOTE_KEY_UP, BT_MOUSE_HOLD, 5600);
    input(REMOTE_KEY_UP, BT_MOUSE_RELEASE, 5700); CHECK(!mouse.movement); // Hold same running pattern.
    input(REMOTE_KEY_DOWN, BT_MOUSE_PRESS, 6000); input(REMOTE_KEY_DOWN, BT_MOUSE_HOLD, 6700);
    tick(6800); CHECK(reports[count - 1].data[2] == 10);
    input(REMOTE_KEY_DOWN, BT_MOUSE_RELEASE, 6900); CHECK(!mouse.movement);
    tap(REMOTE_KEY_OK, 7000); CHECK(mouse.buttons == 1);
    tap(REMOTE_KEY_RIGHT, 7200); tick(7300); CHECK(reports[count - 1].data[0] == 1);
    tap(REMOTE_KEY_OK, 7400); CHECK(!mouse.buttons && mouse.movement == REMOTE_KEY_RIGHT);
    remote.bt.mouse_speed = 1; tick(7520); CHECK(reports[count - 1].data[1] == 2);
    remote.bt.mouse_speed = 10; tick(7540); CHECK(reports[count - 1].data[1] == 20);

    tap(REMOTE_KEY_CUSTOM4, 8000);
    bool positive_x = false, negative_x = false, positive_y = false, negative_y = false, varied = false;
    int previous_x = 0, previous_y = 0;
    for (uint32_t time = 8120; time < 68120; time += 20) {
        before = count; tick(time);
        if (count == before) continue;
        int x = (int8_t)reports[count - 1].data[1], y = (int8_t)reports[count - 1].data[2];
        CHECK(abs(x) <= 20 && abs(y) <= 20);
        positive_x |= x > 0; negative_x |= x < 0; positive_y |= y > 0; negative_y |= y < 0;
        varied |= x != previous_x || y != previous_y;
        previous_x = x; previous_y = y;
    }
    CHECK(positive_x && negative_x && positive_y && negative_y && varied);
    tap(REMOTE_KEY_CUSTOM4, 69000); CHECK(!mouse.movement);
    input(REMOTE_KEY_CUSTOM4, BT_MOUSE_PRESS, 70000);
    input(REMOTE_KEY_CUSTOM4, BT_MOUSE_HOLD, 70700);
    input(REMOTE_KEY_CUSTOM4, BT_MOUSE_RELEASE, 70900); CHECK(!mouse.movement);
    tap(REMOTE_KEY_OK, 71000); tap(REMOTE_KEY_CUSTOM4, 71200);
    bt_mouse_reset(&mouse, 321); before = count; tick(72000);
    CHECK(!mouse.buttons && !mouse.movement && count == before);

    // Attempt the release even if the press fails.
    remote.bt.mouse_mode = BT_MOUSE_MAPPED;
    fail_at = count + 1;
    CHECK(!bt_mouse_input(&mouse, &remote, REMOTE_KEY_VOL_CLICK, BT_MOUSE_PRESS, 73000, &output));
    CHECK(count == fail_at + 1 && !reports[count - 1].data[0]);
    printf("PASS: %u mouse checks (clicks, drag, wheels, patterns, speed, organic motion).\n", checks);
    return 0;
}
