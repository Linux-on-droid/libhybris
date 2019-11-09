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

/**
 * @brief xcb_drihybris_query_version_cookie_t
 **/
typedef struct xcb_drihybris_query_version_cookie_t {
    unsigned int sequence;
} xcb_drihybris_query_version_cookie_t;

/** Opcode for xcb_drihybris_query_version. */
#define XCB_DRIHYBRIS_QUERY_VERSION 0

/**
 * @brief xcb_drihybris_query_version_request_t
 **/
typedef struct xcb_drihybris_query_version_request_t {
    uint8_t  major_opcode;
    uint8_t  minor_opcode;
    uint16_t length;
    uint32_t major_version;
    uint32_t minor_version;
} xcb_drihybris_query_version_request_t;

/**
 * @brief xcb_drihybris_query_version_reply_t
 **/
typedef struct xcb_drihybris_query_version_reply_t {
    uint8_t  response_type;
    uint8_t  pad0;
    uint16_t sequence;
    uint32_t length;
    uint32_t major_version;
    uint32_t minor_version;
} xcb_drihybris_query_version_reply_t;

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

/** Opcode for xcb_drihybris_buffer_from_pixmap. */
#define XCB_DRIHYBRIS_BUFFER_FROM_PIXMAP 2

/**
 * @brief xcb_drihybris_buffer_from_pixmap_request_t
 **/
typedef struct xcb_drihybris_buffer_from_pixmap_request_t {
    uint8_t      major_opcode;
    uint8_t      minor_opcode;
    uint16_t     length;
    xcb_pixmap_t pixmap;
} xcb_drihybris_buffer_from_pixmap_request_t;

/**
 * @brief xcb_drihybris_buffer_from_pixmap_reply_t
 **/
typedef struct xcb_drihybris_buffer_from_pixmap_reply_t {
    uint8_t  response_type;
    uint8_t  nfd;
    uint16_t sequence;
    uint32_t length;
    uint32_t size;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint8_t  depth;
    uint8_t  bpp;
    uint16_t num_ints;
    uint16_t num_fds;
    uint8_t  pad0[8];
} xcb_drihybris_buffer_from_pixmap_reply_t;

/**
 *
 * @param c The connection
 * @return A cookie
 *
 * Delivers a request to the X server.
 *
 */
xcb_drihybris_query_version_cookie_t
xcb_drihybris_query_version (xcb_connection_t *c,
                             uint32_t          major_version,
                             uint32_t          minor_version);

/**
 *
 * @param c The connection
 * @return A cookie
 *
 * Delivers a request to the X server.
 *
 * This form can be used only if the request will cause
 * a reply to be generated. Any returned error will be
 * placed in the event queue.
 */
xcb_drihybris_query_version_cookie_t
xcb_drihybris_query_version_unchecked (xcb_connection_t *c,
                                       uint32_t          major_version,
                                       uint32_t          minor_version);

/**
 * Return the reply
 * @param c      The connection
 * @param cookie The cookie
 * @param e      The xcb_generic_error_t supplied
 *
 * Returns the reply of the request asked by
 *
 * The parameter @p e supplied to this function must be NULL if
 * xcb_drihybris_query_version_unchecked(). is used.
 * Otherwise, it stores the error if any.
 *
 * The returned value must be freed by the caller using free().
 */
xcb_drihybris_query_version_reply_t *
xcb_drihybris_query_version_reply (xcb_connection_t                      *c,
                                   xcb_drihybris_query_version_cookie_t   cookie  /**< */,
                                   xcb_generic_error_t                  **e);

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

int
xcb_drihybris_buffer_from_pixmap_sizeof (const void  *_buffer,
                                         int32_t      fds);

/**
 *
 * @param c The connection
 * @return A cookie
 *
 * Delivers a request to the X server.
 *
 */
xcb_drihybris_buffer_from_pixmap_cookie_t
xcb_drihybris_buffer_from_pixmap (xcb_connection_t *c,
                                  xcb_pixmap_t      pixmap);

/**
 *
 * @param c The connection
 * @return A cookie
 *
 * Delivers a request to the X server.
 *
 * This form can be used only if the request will cause
 * a reply to be generated. Any returned error will be
 * placed in the event queue.
 */
xcb_drihybris_buffer_from_pixmap_cookie_t
xcb_drihybris_buffer_from_pixmap_unchecked (xcb_connection_t *c,
                                            xcb_pixmap_t      pixmap);

uint32_t *
xcb_drihybris_buffer_from_pixmap_ints (const xcb_drihybris_buffer_from_pixmap_reply_t *R);

int
xcb_drihybris_buffer_from_pixmap_ints_length (const xcb_drihybris_buffer_from_pixmap_reply_t *R);

xcb_generic_iterator_t
xcb_drihybris_buffer_from_pixmap_ints_end (const xcb_drihybris_buffer_from_pixmap_reply_t *R);

int32_t *
xcb_drihybris_buffer_from_pixmap_fds (const xcb_drihybris_buffer_from_pixmap_reply_t *R);

/**
 * Return the reply
 * @param c      The connection
 * @param cookie The cookie
 * @param e      The xcb_generic_error_t supplied
 *
 * Returns the reply of the request asked by
 *
 * The parameter @p e supplied to this function must be NULL if
 * xcb_drihybris_buffer_from_pixmap_unchecked(). is used.
 * Otherwise, it stores the error if any.
 *
 * The returned value must be freed by the caller using free().
 */
xcb_drihybris_buffer_from_pixmap_reply_t *
xcb_drihybris_buffer_from_pixmap_reply (xcb_connection_t                           *c,
                                        xcb_drihybris_buffer_from_pixmap_cookie_t   cookie  /**< */,
                                        xcb_generic_error_t                       **e);

/**
 * Return the reply fds
 * @param c      The connection
 * @param reply  The reply
 *
 * Returns the array of reply fds of the request asked by
 *
 * The returned value must be freed by the caller using free().
 */
int *
xcb_drihybris_buffer_from_pixmap_reply_fds (xcb_connection_t                          *c  /**< */,
                                            xcb_drihybris_buffer_from_pixmap_reply_t  *reply);

#ifdef __cplusplus
}
#endif

#endif //DRIHYBRIS_PROTO_H
