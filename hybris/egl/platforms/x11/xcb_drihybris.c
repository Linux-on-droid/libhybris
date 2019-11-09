/*
 * This file generated automatically from drihybris.xml by c_client.py.
 * Edit at your peril.
 */

#include "xcb_drihybris.h"
#include <stddef.h>  /* for offsetof() */

#define ALIGNOF(type) offsetof(struct { char dummy; type member; }, member)

xcb_extension_t xcb_drihybris_id = { "DRIHYBRIS", 0 };

xcb_drihybris_query_version_cookie_t
xcb_drihybris_query_version (xcb_connection_t *c,
                             uint32_t          major_version,
                             uint32_t          minor_version)
{
    static const xcb_protocol_request_t xcb_req = {
        .count = 2,
        .ext = &xcb_drihybris_id,
        .opcode = XCB_DRIHYBRIS_QUERY_VERSION,
        .isvoid = 0
    };

    struct iovec xcb_parts[4];
    xcb_drihybris_query_version_cookie_t xcb_ret;
    xcb_drihybris_query_version_request_t xcb_out;

    xcb_out.major_version = major_version;
    xcb_out.minor_version = minor_version;

    xcb_parts[2].iov_base = (char *) &xcb_out;
    xcb_parts[2].iov_len = sizeof(xcb_out);
    xcb_parts[3].iov_base = 0;
    xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;

    xcb_ret.sequence = xcb_send_request(c, XCB_REQUEST_CHECKED, xcb_parts + 2, &xcb_req);
    return xcb_ret;
}

xcb_drihybris_query_version_cookie_t
xcb_drihybris_query_version_unchecked (xcb_connection_t *c,
                                       uint32_t          major_version,
                                       uint32_t          minor_version)
{
    static const xcb_protocol_request_t xcb_req = {
        .count = 2,
        .ext = &xcb_drihybris_id,
        .opcode = XCB_DRIHYBRIS_QUERY_VERSION,
        .isvoid = 0
    };

    struct iovec xcb_parts[4];
    xcb_drihybris_query_version_cookie_t xcb_ret;
    xcb_drihybris_query_version_request_t xcb_out;

    xcb_out.major_version = major_version;
    xcb_out.minor_version = minor_version;

    xcb_parts[2].iov_base = (char *) &xcb_out;
    xcb_parts[2].iov_len = sizeof(xcb_out);
    xcb_parts[3].iov_base = 0;
    xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;

    xcb_ret.sequence = xcb_send_request(c, 0, xcb_parts + 2, &xcb_req);
    return xcb_ret;
}

xcb_drihybris_query_version_reply_t *
xcb_drihybris_query_version_reply (xcb_connection_t                      *c,
                                   xcb_drihybris_query_version_cookie_t   cookie  /**< */,
                                   xcb_generic_error_t                  **e)
{
    return (xcb_drihybris_query_version_reply_t *) xcb_wait_for_reply(c, cookie.sequence, e);
}

int
xcb_drihybris_pixmap_from_buffer_sizeof (const void  *_buffer,
                                         int32_t      pixmap_fd)
{
    char *xcb_tmp = (char *)_buffer;
    const xcb_drihybris_pixmap_from_buffer_request_t *_aux = (xcb_drihybris_pixmap_from_buffer_request_t *)_buffer;
    unsigned int xcb_buffer_len = 0;
    unsigned int xcb_block_len = 0;
    unsigned int xcb_pad = 0;
    unsigned int xcb_align_to = 0;


    xcb_block_len += sizeof(xcb_drihybris_pixmap_from_buffer_request_t);
    xcb_tmp += xcb_block_len;
    xcb_buffer_len += xcb_block_len;
    xcb_block_len = 0;
    /* ints */
    xcb_block_len += _aux->num_ints * sizeof(uint32_t);
    xcb_tmp += xcb_block_len;
    xcb_align_to = ALIGNOF(uint32_t);
    /* insert padding */
    xcb_pad = -xcb_block_len & (xcb_align_to - 1);
    xcb_buffer_len += xcb_block_len + xcb_pad;
    if (0 != xcb_pad) {
        xcb_tmp += xcb_pad;
        xcb_pad = 0;
    }
    xcb_block_len = 0;

    return xcb_buffer_len;
}

xcb_void_cookie_t
xcb_drihybris_pixmap_from_buffer_checked (xcb_connection_t *c,
                                          xcb_pixmap_t      pixmap,
                                          xcb_drawable_t    drawable,
                                          uint32_t          size,
                                          uint16_t          width,
                                          uint16_t          height,
                                          uint16_t          stride,
                                          uint8_t           depth,
                                          uint8_t           bpp,
                                          uint16_t          num_ints,
                                          uint16_t          num_fds,
                                          const uint32_t   *ints,
                                          const int32_t    *fds)
{
    static const xcb_protocol_request_t xcb_req = {
        .count = 4,
        .ext = &xcb_drihybris_id,
        .opcode = XCB_DRIHYBRIS_PIXMAP_FROM_BUFFER,
        .isvoid = 1
    };

    struct iovec xcb_parts[6];
    xcb_void_cookie_t xcb_ret;
    xcb_drihybris_pixmap_from_buffer_request_t xcb_out;

    xcb_out.pixmap = pixmap;
    xcb_out.drawable = drawable;
    xcb_out.size = size;
    xcb_out.width = width;
    xcb_out.height = height;
    xcb_out.stride = stride;
    xcb_out.depth = depth;
    xcb_out.bpp = bpp;
    xcb_out.num_ints = num_ints;
    xcb_out.num_fds = num_fds;

    xcb_parts[2].iov_base = (char *) &xcb_out;
    xcb_parts[2].iov_len = sizeof(xcb_out);
    xcb_parts[3].iov_base = 0;
    xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;
    /* uint32_t ints */
    xcb_parts[4].iov_base = (char *) ints;
    xcb_parts[4].iov_len = num_ints * sizeof(uint32_t);
    xcb_parts[5].iov_base = 0;
    xcb_parts[5].iov_len = -xcb_parts[4].iov_len & 3;

    xcb_ret.sequence = xcb_send_request_with_fds(c, XCB_REQUEST_CHECKED, xcb_parts + 2, &xcb_req, num_fds, fds);
    return xcb_ret;
}

xcb_void_cookie_t
xcb_drihybris_pixmap_from_buffer (xcb_connection_t *c,
                                  xcb_pixmap_t      pixmap,
                                  xcb_drawable_t    drawable,
                                  uint32_t          size,
                                  uint16_t          width,
                                  uint16_t          height,
                                  uint16_t          stride,
                                  uint8_t           depth,
                                  uint8_t           bpp,
                                  uint16_t          num_ints,
                                  uint16_t          num_fds,
                                  const uint32_t   *ints,
                                  const int32_t    *fds)
{
    static const xcb_protocol_request_t xcb_req = {
        .count = 4,
        .ext = &xcb_drihybris_id,
        .opcode = XCB_DRIHYBRIS_PIXMAP_FROM_BUFFER,
        .isvoid = 1
    };

    struct iovec xcb_parts[6];
    xcb_void_cookie_t xcb_ret;
    xcb_drihybris_pixmap_from_buffer_request_t xcb_out;

    xcb_out.pixmap = pixmap;
    xcb_out.drawable = drawable;
    xcb_out.size = size;
    xcb_out.width = width;
    xcb_out.height = height;
    xcb_out.stride = stride;
    xcb_out.depth = depth;
    xcb_out.bpp = bpp;
    xcb_out.num_ints = num_ints;
    xcb_out.num_fds = num_fds;

    xcb_parts[2].iov_base = (char *) &xcb_out;
    xcb_parts[2].iov_len = sizeof(xcb_out);
    xcb_parts[3].iov_base = 0;
    xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;
    /* uint32_t ints */
    xcb_parts[4].iov_base = (char *) ints;
    xcb_parts[4].iov_len = num_ints * sizeof(uint32_t);
    xcb_parts[5].iov_base = 0;
    xcb_parts[5].iov_len = -xcb_parts[4].iov_len & 3;

    xcb_ret.sequence = xcb_send_request_with_fds(c, 0, xcb_parts + 2, &xcb_req, num_fds, fds);
    return xcb_ret;
}

uint32_t *
xcb_drihybris_pixmap_from_buffer_ints (const xcb_drihybris_pixmap_from_buffer_request_t *R)
{
    return (uint32_t *) (R + 1);
}

int
xcb_drihybris_pixmap_from_buffer_ints_length (const xcb_drihybris_pixmap_from_buffer_request_t *R)
{
    return R->num_ints;
}

xcb_generic_iterator_t
xcb_drihybris_pixmap_from_buffer_ints_end (const xcb_drihybris_pixmap_from_buffer_request_t *R)
{
    xcb_generic_iterator_t i;
    i.data = ((uint32_t *) (R + 1)) + (R->num_ints);
    i.rem = 0;
    i.index = (char *) i.data - (char *) R;
    return i;
}

int
xcb_drihybris_buffer_from_pixmap_sizeof (const void  *_buffer,
                                         int32_t      fds)
{
    char *xcb_tmp = (char *)_buffer;
    const xcb_drihybris_buffer_from_pixmap_reply_t *_aux = (xcb_drihybris_buffer_from_pixmap_reply_t *)_buffer;
    unsigned int xcb_buffer_len = 0;
    unsigned int xcb_block_len = 0;
    unsigned int xcb_pad = 0;
    unsigned int xcb_align_to = 0;


    xcb_block_len += sizeof(xcb_drihybris_buffer_from_pixmap_reply_t);
    xcb_tmp += xcb_block_len;
    xcb_buffer_len += xcb_block_len;
    xcb_block_len = 0;
    /* ints */
    xcb_block_len += _aux->num_ints * sizeof(uint32_t);
    xcb_tmp += xcb_block_len;
    xcb_align_to = ALIGNOF(uint32_t);
    /* insert padding */
    xcb_pad = -xcb_block_len & (xcb_align_to - 1);
    xcb_buffer_len += xcb_block_len + xcb_pad;
    if (0 != xcb_pad) {
        xcb_tmp += xcb_pad;
        xcb_pad = 0;
    }
    xcb_block_len = 0;

    return xcb_buffer_len;
}

xcb_drihybris_buffer_from_pixmap_cookie_t
xcb_drihybris_buffer_from_pixmap (xcb_connection_t *c,
                                  xcb_pixmap_t      pixmap)
{
    static const xcb_protocol_request_t xcb_req = {
        .count = 2,
        .ext = &xcb_drihybris_id,
        .opcode = XCB_DRIHYBRIS_BUFFER_FROM_PIXMAP,
        .isvoid = 0
    };

    struct iovec xcb_parts[4];
    xcb_drihybris_buffer_from_pixmap_cookie_t xcb_ret;
    xcb_drihybris_buffer_from_pixmap_request_t xcb_out;

    xcb_out.pixmap = pixmap;

    xcb_parts[2].iov_base = (char *) &xcb_out;
    xcb_parts[2].iov_len = sizeof(xcb_out);
    xcb_parts[3].iov_base = 0;
    xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;

    xcb_ret.sequence = xcb_send_request(c, XCB_REQUEST_CHECKED|XCB_REQUEST_REPLY_FDS, xcb_parts + 2, &xcb_req);
    return xcb_ret;
}

xcb_drihybris_buffer_from_pixmap_cookie_t
xcb_drihybris_buffer_from_pixmap_unchecked (xcb_connection_t *c,
                                            xcb_pixmap_t      pixmap)
{
    static const xcb_protocol_request_t xcb_req = {
        .count = 2,
        .ext = &xcb_drihybris_id,
        .opcode = XCB_DRIHYBRIS_BUFFER_FROM_PIXMAP,
        .isvoid = 0
    };

    struct iovec xcb_parts[4];
    xcb_drihybris_buffer_from_pixmap_cookie_t xcb_ret;
    xcb_drihybris_buffer_from_pixmap_request_t xcb_out;

    xcb_out.pixmap = pixmap;

    xcb_parts[2].iov_base = (char *) &xcb_out;
    xcb_parts[2].iov_len = sizeof(xcb_out);
    xcb_parts[3].iov_base = 0;
    xcb_parts[3].iov_len = -xcb_parts[2].iov_len & 3;

    xcb_ret.sequence = xcb_send_request(c, XCB_REQUEST_REPLY_FDS, xcb_parts + 2, &xcb_req);
    return xcb_ret;
}

uint32_t *
xcb_drihybris_buffer_from_pixmap_ints (const xcb_drihybris_buffer_from_pixmap_reply_t *R)
{
    return (uint32_t *) (R + 1);
}

int
xcb_drihybris_buffer_from_pixmap_ints_length (const xcb_drihybris_buffer_from_pixmap_reply_t *R)
{
    return R->num_ints;
}

xcb_generic_iterator_t
xcb_drihybris_buffer_from_pixmap_ints_end (const xcb_drihybris_buffer_from_pixmap_reply_t *R)
{
    xcb_generic_iterator_t i;
    i.data = ((uint32_t *) (R + 1)) + (R->num_ints);
    i.rem = 0;
    i.index = (char *) i.data - (char *) R;
    return i;
}

int32_t *
xcb_drihybris_buffer_from_pixmap_fds (const xcb_drihybris_buffer_from_pixmap_reply_t *R)
{
    xcb_generic_iterator_t prev = xcb_drihybris_buffer_from_pixmap_ints_end(R);
    return (int32_t *) ((char *) prev.data + XCB_TYPE_PAD(int32_t, prev.index) + 0);
}

xcb_drihybris_buffer_from_pixmap_reply_t *
xcb_drihybris_buffer_from_pixmap_reply (xcb_connection_t                           *c,
                                        xcb_drihybris_buffer_from_pixmap_cookie_t   cookie  /**< */,
                                        xcb_generic_error_t                       **e)
{
    return (xcb_drihybris_buffer_from_pixmap_reply_t *) xcb_wait_for_reply(c, cookie.sequence, e);
}

int *
xcb_drihybris_buffer_from_pixmap_reply_fds (xcb_connection_t                          *c  /**< */,
                                            xcb_drihybris_buffer_from_pixmap_reply_t  *reply)
{
    return xcb_get_reply_fds(c, reply, sizeof(xcb_drihybris_buffer_from_pixmap_reply_t) + 4 * reply->length);
}

