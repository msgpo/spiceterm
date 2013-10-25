/*

     Copyright (C) 2013 Proxmox Server Solutions GmbH

     Copyright: spiceterm is under GNU GPL, the GNU General Public License.

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; version 2 dated June, 1991.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
     02111-1307, USA.

     Author: Dietmar Maurer <dietmar@proxmox.com>
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>

#include "spiceterm.h"

#include <glib.h>
#include <spice.h>
#include <spice/enums.h>
#include <spice/macros.h>
#include <spice/qxl_dev.h>

#include "event_loop.h"

static int debug = 0;

#define DPRINTF(x, format, ...) { \
    if (x <= debug) { \
        printf("%s: " format "\n" , __FUNCTION__, ## __VA_ARGS__); \
    } \
}

static uint8_t
my_kbd_get_leds(SpiceKbdInstance *sin)
{
    return 0;
}

#define KBD_MOD_CONTROL_L_FLAG (1<<0)
#define KBD_MOD_CONTROL_R_FLAG (1<<1)
#define KBD_MOD_SHIFT_L_FLAG (1<<2)
#define KBD_MOD_SHIFT_R_FLAG (1<<3)

static int kbd_flags = 0;

static void
my_kbd_push_key(SpiceKbdInstance *sin, uint8_t frag)
{
    spiceTerm *vt = SPICE_CONTAINEROF(sin, spiceTerm, keyboard_sin);

    char *esc = NULL; // used to send special keys

    static int e0_mode = 0;

    DPRINTF(1, "enter frag=%02x flags=%08x", frag, kbd_flags);

    if (e0_mode) {
        e0_mode = 0;
        switch (frag) {
        case 0x1d: // press Control_R
            kbd_flags |= KBD_MOD_CONTROL_R_FLAG;
            break;
        case  0x9d: // release Control_R
            kbd_flags &= ~KBD_MOD_CONTROL_R_FLAG;
            break;
        case 0x47: // press Home
            esc = "OH";
            break;
        case 0x4f: // press END
            esc = "OF";
            break;
        case 0x48: // press UP
            esc = "OA";
            break;
        case 0x50: // press DOWN
            esc = "OB";
            break;
        case 0x4b: // press LEFT
            esc = "OD";
            break;
        case 0x4d: // press RIGHT
            esc = "OC";
            break;
        case 0x52: // press INSERT
            esc = "[2~";
            break;
        case 0x49: // press PAGE_UP
            if (kbd_flags & (KBD_MOD_SHIFT_L_FLAG|KBD_MOD_SHIFT_R_FLAG)) {
                spiceterm_virtual_scroll(vt, -vt->height/2);
            }
            break;
        case 0x51: // press PAGE_DOWN
            if (kbd_flags & (KBD_MOD_SHIFT_L_FLAG|KBD_MOD_SHIFT_R_FLAG)) {
                spiceterm_virtual_scroll(vt, vt->height/2);
            }
            break;
        }
    } else {
        switch (frag) {
        case 0xe0:
            e0_mode = 1;
            break;
        case 0x1d: // press Control_L
            kbd_flags |= KBD_MOD_CONTROL_L_FLAG;
            break;
        case 0x9d: // release Control_L
            kbd_flags &= ~KBD_MOD_CONTROL_L_FLAG;
            break;
        case 0x2a: // press Shift_L
            kbd_flags |= KBD_MOD_SHIFT_L_FLAG;
            break;
        case 0xaa: // release Shift_L
            kbd_flags &= ~KBD_MOD_SHIFT_L_FLAG;
            break;
        case 0x36: // press Shift_R
            kbd_flags |= KBD_MOD_SHIFT_R_FLAG;
            break;
        case 0xb6: // release Shift_R
            kbd_flags &= ~KBD_MOD_SHIFT_R_FLAG;
            break;
        case 0x52: // press KP_INSERT
            esc = "[2~";
            break;
        case 0x53: // press KP_Delete
            esc = "[3~";
            break;
        case 0x47: // press KP_Home
            esc = "OH";
            break;
        case 0x4f: // press KP_END
            esc = "OF";
            break;
        case 0x48: // press KP_UP
            esc = "OA";
            break;
        case 0x50: // press KP_DOWN
            esc = "OB";
            break;
        case 0x4b: // press KP_LEFT
            esc = "OD";
            break;
        case 0x4d: // press KP_RIGHT
            esc = "OC";
            break;
        case 0x3b: // press F1
            esc = "OP";
            break;
        case 0x3c: // press F2
            esc = "OQ";
            break;
        case 0x3d: // press F3
            esc = "OR";
            break;
        case 0x3e: // press F4
            esc = "OS";
            break;
        case 0x3f: // press F5
            esc = "[15~";
            break;
        case 0x40: // press F6
            esc = "[17~";
            break;
        case 0x41: // press F7
            esc = "[18~";
            break;
        case 0x42: // press F8
            esc = "[19~";
            break;
        case 0x43: // press F9
            esc = "[20~";
            break;
        case 0x44: // press F10
            esc = "[21~";
            break;
        case 0x57: // press F11
            esc = "[23~";
            break;
        case 0x58: // press F12
            esc = "[24~";
            break;
        }
    }

    if (esc) {
        DPRINTF(1, "escape=%s", esc);
        spiceterm_respond_esc(vt, esc);

        if (vt->y_displ != vt->y_base) {
            vt->y_displ = vt->y_base;
            spiceterm_refresh(vt);
        }

        spiceterm_update_watch_mask(vt, TRUE);
    }

    DPRINTF(1, "leave frag=%02x flags=%08x", frag, kbd_flags);
    return;
}

static void
my_kbd_push_utf8(SpiceKbdInstance *sin, uint32_t size, uint8_t *data)
{
    spiceTerm *vt = SPICE_CONTAINEROF(sin, spiceTerm, keyboard_sin);

    DPRINTF(1, " size=%d data[0]=%02x", size, data[0]);
    
    if (kbd_flags & (KBD_MOD_CONTROL_L_FLAG|KBD_MOD_CONTROL_R_FLAG)) {
        if (size != 1) return;
        if (data[0] >= 'a' && data[0] <= 'z') {
            uint8_t buf[1] = { data[0] - 'a' + 1 };
            spiceterm_respond_data(vt, 1, buf);

        } else if (data[0] >= 'A' && data[0] <= 'Z') {
            uint8_t buf[1] = { data[0] - 'A' + 1 };
            spiceterm_respond_data(vt, 1, buf);           
        }
    } else {
        if (size == 1 && data[0] == 0x7f) {
            /* use an escape sequence for DELETE, else it behaves like BACKSPACE */
            spiceterm_respond_esc(vt, "[3~");
        } else {
            spiceterm_respond_data(vt, size, data);
        }
    }
    
    if (vt->y_displ != vt->y_base) {
        vt->y_displ = vt->y_base;
        spiceterm_refresh(vt);
    }

    spiceterm_update_watch_mask(vt, TRUE);

    return;
}

static SpiceKbdInterface my_keyboard_sif = {
    .base.type          = SPICE_INTERFACE_KEYBOARD,
    .base.description   = "spiceterm keyboard device",
    .base.major_version = SPICE_INTERFACE_KEYBOARD_MAJOR,
    .base.minor_version = SPICE_INTERFACE_KEYBOARD_MINOR,
    .push_scan_freg     = my_kbd_push_key,
    .get_leds           = my_kbd_get_leds,
    .push_utf8          = my_kbd_push_utf8,
};


/* vdagent interface - to get mouse/clipboard support */

#define VDAGENT_WBUF_SIZE (1024*50)
static unsigned char vdagent_write_buffer[VDAGENT_WBUF_SIZE];
static int vdagent_write_buffer_pos = 0;
static int agent_owns_clipboard[256] = { 0, };

static void  
vdagent_reply(spiceTerm *vt, uint32_t type, uint32_t error)
{
    uint32_t size;
    
    size = sizeof(VDAgentReply);

    int msg_size =  sizeof(VDIChunkHeader) + sizeof(VDAgentMessage) + size;
    g_assert((vdagent_write_buffer_pos + msg_size) < VDAGENT_WBUF_SIZE);

    unsigned char *buf = vdagent_write_buffer + vdagent_write_buffer_pos;
    vdagent_write_buffer_pos += msg_size;

    memset(buf, 0, msg_size);

    VDIChunkHeader *hdr = (VDIChunkHeader *)buf;
    VDAgentMessage *msg = (VDAgentMessage *)&hdr[1];
    VDAgentReply *reply = (VDAgentReply *)&msg[1];
    reply->type = type;
    reply->error = error;

    hdr->port = VDP_CLIENT_PORT;
    hdr->size = sizeof(VDAgentMessage) + size;

    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = VD_AGENT_REPLY;
    msg->opaque = 0;
    msg->size = size;

    spice_server_char_device_wakeup(&vt->vdagent_sin);
}

static void
dump_message(unsigned char *buf, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        printf("%d  %02X\n", i, buf[i]);
    }

    // exit(0);
}

static void 
vdagent_send_capabilities(spiceTerm *vt, uint32_t request)
{
    VDAgentAnnounceCapabilities *caps;
    uint32_t size;

    size = sizeof(*caps) + VD_AGENT_CAPS_BYTES;
    caps = calloc(1, size);
    g_assert(caps != NULL);

    caps->request = request;
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_MOUSE_STATE);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_MONITORS_CONFIG);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_REPLY);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_CLIPBOARD_SELECTION);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_SPARSE_MONITORS_CONFIG);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_GUEST_LINEEND_LF);

    int msg_size =  sizeof(VDIChunkHeader) + sizeof(VDAgentMessage) + size;
    g_assert((vdagent_write_buffer_pos + msg_size) < VDAGENT_WBUF_SIZE);

    unsigned char *buf = vdagent_write_buffer + vdagent_write_buffer_pos;
    vdagent_write_buffer_pos += msg_size;

    VDIChunkHeader *hdr = (VDIChunkHeader *)buf;
    VDAgentMessage *msg = (VDAgentMessage *)&hdr[1];
 
    hdr->port = VDP_CLIENT_PORT;
    hdr->size = sizeof(VDAgentMessage) + size;
    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = VD_AGENT_ANNOUNCE_CAPABILITIES;
    msg->opaque = 0;
    msg->size = size;

    memcpy(buf + sizeof(VDIChunkHeader) + sizeof(VDAgentMessage), (uint8_t *)caps, size);

    if (0) dump_message(buf, msg_size);

    spice_server_char_device_wakeup(&vt->vdagent_sin);

    free(caps);
}

gboolean 
vdagent_owns_clipboard(spiceTerm *vt)
{
    return !!agent_owns_clipboard[VD_AGENT_CLIPBOARD_SELECTION_PRIMARY];
}

void 
vdagent_grab_clipboard(spiceTerm *vt)
{
    uint32_t size;
    
    uint8_t selection = VD_AGENT_CLIPBOARD_SELECTION_PRIMARY;

    agent_owns_clipboard[selection] = 1;

    size = 8;

    int msg_size =  sizeof(VDIChunkHeader) + sizeof(VDAgentMessage) + size;
    g_assert((vdagent_write_buffer_pos + msg_size) < VDAGENT_WBUF_SIZE);

    unsigned char *buf = vdagent_write_buffer + vdagent_write_buffer_pos;
    vdagent_write_buffer_pos += msg_size;

    memset(buf, 0, msg_size);

    VDIChunkHeader *hdr = (VDIChunkHeader *)buf;
    VDAgentMessage *msg = (VDAgentMessage *)&hdr[1];
    uint8_t *grab = (uint8_t *)&msg[1];
    *((uint8_t *)grab) = selection;
    *((uint32_t *)(grab + 4)) = VD_AGENT_CLIPBOARD_UTF8_TEXT;

    hdr->port = VDP_CLIENT_PORT;
    hdr->size = sizeof(VDAgentMessage) + size;

    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = VD_AGENT_CLIPBOARD_GRAB;
    msg->opaque = 0;
    msg->size = size;

    if (0) dump_message(buf, msg_size);

    spice_server_char_device_wakeup(&vt->vdagent_sin);
}

void 
vdagent_request_clipboard(spiceTerm *vt)
{
    uint32_t size;

    uint8_t selection = VD_AGENT_CLIPBOARD_SELECTION_PRIMARY;
    
    size = 4 + sizeof(VDAgentClipboardRequest);

    int msg_size =  sizeof(VDIChunkHeader) + sizeof(VDAgentMessage) + size;
    g_assert((vdagent_write_buffer_pos + msg_size) < VDAGENT_WBUF_SIZE);

    unsigned char *buf = vdagent_write_buffer + vdagent_write_buffer_pos;
    vdagent_write_buffer_pos += msg_size;

    memset(buf, 0, msg_size);

    VDIChunkHeader *hdr = (VDIChunkHeader *)buf;
    VDAgentMessage *msg = (VDAgentMessage *)&hdr[1];
    uint8_t *data = (uint8_t *)&msg[1];
    *((uint32_t *)data) = 0;
    data[0] = selection;
    ((uint32_t *)data)[1] = VD_AGENT_CLIPBOARD_UTF8_TEXT;

    hdr->port = VDP_CLIENT_PORT;
    hdr->size = sizeof(VDAgentMessage) + size;

    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = VD_AGENT_CLIPBOARD_REQUEST;
    msg->opaque = 0;
    msg->size = size;

    if (0) dump_message(buf, msg_size);

    spice_server_char_device_wakeup(&vt->vdagent_sin);
}

static void 
vdagent_send_clipboard(spiceTerm *vt, uint8_t selection)
{
    uint32_t size;
    
    if (selection != VD_AGENT_CLIPBOARD_SELECTION_PRIMARY) {
        fprintf(stderr, "clipboard select %d is not supported\n", selection);
        return;
    }

    gchar *sel_data;
    glong sel_len;
    if (vt->utf8) {
        sel_data = g_utf16_to_utf8(vt->selection, vt->selection_len, NULL, &sel_len, NULL);
    } else {
        sel_len = vt->selection_len;
        sel_data = g_malloc(sel_len);
        int i;
        for (i = 0; i < sel_len; i++) { sel_data[i] =  (char)vt->selection[i]; }
        sel_data[sel_len] = 0;
    }

    size = 8 + sel_len;

    int msg_size =  sizeof(VDIChunkHeader) + sizeof(VDAgentMessage) + size;
    g_assert((vdagent_write_buffer_pos + msg_size) < VDAGENT_WBUF_SIZE);

    unsigned char *buf = vdagent_write_buffer + vdagent_write_buffer_pos;
    vdagent_write_buffer_pos += msg_size;

    memset(buf, 0, msg_size);
   
    VDIChunkHeader *hdr = (VDIChunkHeader *)buf;
    VDAgentMessage *msg = (VDAgentMessage *)&hdr[1];
    uint8_t *data = (uint8_t *)&msg[1];
    *((uint8_t *)data) = selection;
    data += 4;
    *((uint32_t *)data) = VD_AGENT_CLIPBOARD_UTF8_TEXT;
    data += 4;

    memcpy(data, sel_data, sel_len);
    g_free(sel_data);

    hdr->port = VDP_CLIENT_PORT;
    hdr->size = sizeof(VDAgentMessage) + size;

    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = VD_AGENT_CLIPBOARD;
    msg->opaque = 0;
    msg->size = size;

    spice_server_char_device_wakeup(&vt->vdagent_sin);
}

static int
vmc_write(SpiceCharDeviceInstance *sin, const uint8_t *buf, int len)
{
    spiceTerm *vt = SPICE_CONTAINEROF(sin, spiceTerm, vdagent_sin);

    VDIChunkHeader *hdr = (VDIChunkHeader *)buf;
    VDAgentMessage *msg = (VDAgentMessage *)&hdr[1];

    //g_assert(hdr->port == VDP_SERVER_PORT);
    g_assert(msg->protocol == VD_AGENT_PROTOCOL);

    DPRINTF(1, "%d %d %d %d", len, hdr->port, msg->protocol, msg->type);

    switch (msg->type) {
    case VD_AGENT_MOUSE_STATE: { 
        VDAgentMouseState *info = (VDAgentMouseState *)&msg[1];
        spiceterm_motion_event(vt, info->x, info->y, info->buttons);
        break;
    }
    case VD_AGENT_ANNOUNCE_CAPABILITIES: {
        VDAgentAnnounceCapabilities *caps = (VDAgentAnnounceCapabilities *)&msg[1];
        DPRINTF(1, "VD_AGENT_ANNOUNCE_CAPABILITIES %d", caps->request);
        int i;
        
        int caps_size = VD_AGENT_CAPS_SIZE_FROM_MSG_SIZE(hdr->size);
        for (i = 0; i < VD_AGENT_END_CAP; i++) {
            DPRINTF(1, "CAPABILITIES %d %d", i, VD_AGENT_HAS_CAPABILITY(caps->caps, caps_size, i));
        }

        vdagent_send_capabilities(vt, 0);
        break;
    }
    case VD_AGENT_CLIPBOARD_GRAB: {
        VDAgentClipboardGrab *grab = (VDAgentClipboardGrab *)&msg[1];
        uint8_t selection = *((uint8_t *)grab);
        DPRINTF(1, "VD_AGENT_CLIPBOARD_GRAB %d", selection);
        agent_owns_clipboard[selection] = 0;
        spiceterm_clear_selection(vt);
        break;
    }
    case VD_AGENT_CLIPBOARD_REQUEST: {
        uint8_t *req = (uint8_t *)&msg[1];
        uint8_t selection = *((uint8_t *)req);
        uint32_t type = *((uint32_t *)(req + 4));

        DPRINTF(1, "VD_AGENT_CLIPBOARD_REQUEST %d %d", selection, type);

        vdagent_send_clipboard(vt, selection);
        
        break;
    }
    case VD_AGENT_CLIPBOARD: {
        uint8_t *data = (uint8_t *)&msg[1];
        uint8_t selection = data[0];
        uint32_t type = *(uint32_t *)(data + 4);
        int size = msg->size - 8;
        DPRINTF(1, "VD_AGENT_CLIPBOARD %d %d %d", selection, type, size);

        if (type == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
            spiceterm_respond_data(vt, size, data + 8);
            spiceterm_update_watch_mask(vt, TRUE);
        }
        break;
    }
    case VD_AGENT_CLIPBOARD_RELEASE: {
        uint8_t *data = (uint8_t *)&msg[1];
        uint8_t selection = data[0];
        
        DPRINTF(1, "VD_AGENT_CLIPBOARD_RELEASE %d", selection);
     
        break;
    }
    case VD_AGENT_MONITORS_CONFIG: {
        VDAgentMonitorsConfig *list = (VDAgentMonitorsConfig *)&msg[1];
        g_assert(list->num_of_monitors > 0);
        DPRINTF(1, "VD_AGENT_MONITORS_CONFIG %d %d %d", list->num_of_monitors, 
                list->monitors[0].width, list->monitors[0].height);
        
        spiceterm_resize(vt, list->monitors[0].width, list->monitors[0].height);

        vdagent_reply(vt, VD_AGENT_MONITORS_CONFIG, VD_AGENT_SUCCESS);
        break;
    }
    default:
        DPRINTF(1, "got uknown vdagent message type %d\n", msg->type);
    }

    return len;
}

static int
vmc_read(SpiceCharDeviceInstance *sin, uint8_t *buf, int len)
{
    DPRINTF(1, "%d %d", len,  vdagent_write_buffer_pos);
    g_assert(len >= 8);

    if (!vdagent_write_buffer_pos) {
        return 0;
    }
     
    int size = (len >= vdagent_write_buffer_pos) ? vdagent_write_buffer_pos : len;
    memcpy(buf, vdagent_write_buffer, size);
    if (size < vdagent_write_buffer_pos) {
        memmove(vdagent_write_buffer, vdagent_write_buffer + size, 
                vdagent_write_buffer_pos - size);
    }
    vdagent_write_buffer_pos -= size;

    DPRINTF(1, "RET %d %d", size,  vdagent_write_buffer_pos);
    return size;
}

static void
vmc_state(SpiceCharDeviceInstance *sin, int connected)
{
    /* IGNORE */
}

static SpiceCharDeviceInterface my_vdagent_sif = {
    .base.type          = SPICE_INTERFACE_CHAR_DEVICE,
    .base.description   = "spice virtual channel char device",
    .base.major_version = SPICE_INTERFACE_CHAR_DEVICE_MAJOR,
    .base.minor_version = SPICE_INTERFACE_CHAR_DEVICE_MINOR,
    .state              = vmc_state,
    .write              = vmc_write,
    .read               = vmc_read,
};

spiceTerm *
spiceterm_create(uint32_t width, uint32_t height, SpiceTermOptions *opts)
{
    SpiceCoreInterface *core = basic_event_loop_init();
    SpiceScreen *spice_screen = spice_screen_new(core, width, height, opts);

    //spice_server_set_image_compression(server, SPICE_IMAGE_COMPRESS_OFF);
        
    spice_screen->image_cache = g_hash_table_new(g_int_hash, g_int_equal);

    spiceTerm *vt = (spiceTerm *)calloc (sizeof(spiceTerm), 1);

    vt->keyboard_sin.base.sif = &my_keyboard_sif.base;
    spice_server_add_interface(spice_screen->server, &vt->keyboard_sin.base);

    vt->vdagent_sin.base.sif = &my_vdagent_sif.base;
    vt->vdagent_sin.subtype = "vdagent";
    spice_server_add_interface(spice_screen->server, &vt->vdagent_sin.base);
    vt->screen = spice_screen;

    init_spiceterm(vt, width, height);

    return vt;
}
