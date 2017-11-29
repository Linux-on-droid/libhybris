/*
 * This file generated automatically from drihybris.xml by c_client.py.
 * Edit at your peril.
 */

/**
 * @defgroup XCB_DRIHYBRIS_API XCB DRIHYBRIS API
 * @brief DRIHYBRIS XCB Protocol Implementation.
 * @{
 **/

#ifndef DRIHYBRIS_PROTO_H
#define DRIHYBRIS_PROTO_H

#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XCB_DRIHYBRIS_MAJOR_VERSION 1
#define XCB_DRIHYBRIS_MINOR_VERSION 0

extern xcb_extension_t xcb_drihybris_id;

/** Opcode for xcb_drihybris_pixmap_from_buffer. */
#define XCB_DRIHYBRIS_PIXMAP_FROM_BUFFER 1

/**
 * @brief xcb_drihybris_pixmap_from_buffer_request_t
 **/
typedef struct xcb_drihybris_pixmap_from_buffer_request_t {
    uint8_t        major_opcode;
    uint8_t        minor_opcode;
    uint16_t       length;
    xcb_pixmap_t   pixmap;
    xcb_drawable_t drawable;
    uint32_t       size;
    uint16_t       width;
    uint16_t       height;
    uint16_t       stride;
    uint8_t        depth;
    uint8_t        bpp;
    uint16_t       num_ints;
    uint16_t       num_fds;
} xcb_drihybris_pixmap_from_buffer_request_t;

/**
 * @brief xcb_drihybris_buffer_from_pixmap_cookie_t
 **/
typedef struct xcb_drihybris_buffer_from_pixmap_cookie_t {
    unsigned int sequence;
} xcb_drihybris_buffer_from_pixmap_cookie_t;

int
xcb_drihybris_pixmap_from_buffer_sizeof (const void  *_buffer,
                                         int32_t      pixmap_fd);

/**
 *
 * @param c The connection
 * @return A cookie
 *
 * Delivers a request to the X server.
 *
 * This form can be used only if the request will not cause
 * a reply to be generated. Any returned error will be
 * saved for handling by xcb_request_check().
 */
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
                                          const int32_t    *fds);

/**
 *
 * @param c The connection
 * @return A cookie
 *
 * Delivers a request to the X server.
 *
 */
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
                                  const int32_t    *fds);

uint32_t *
xcb_drihybris_pixmap_from_buffer_ints (const xcb_drihybris_pixmap_from_buffer_request_t *R);

int
xcb_drihybris_pixmap_from_buffer_ints_length (const xcb_drihybris_pixmap_from_buffer_request_t *R);

xcb_generic_iterator_t
xcb_drihybris_pixmap_from_buffer_ints_end (const xcb_drihybris_pixmap_from_buffer_request_t *R);

#ifdef __cplusplus
}
#endif

#endif //DRIHYBRIS_PROTO_H
