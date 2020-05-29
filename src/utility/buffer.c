/*
 * Dynamic/rolling memory buffer, which supports stream-like data processing.
 *
 * Copyright (C) 1999-2020 J.M. Heisz.  All Rights Reserved.
 * See the LICENSE file accompanying the distribution your rights to use
 * this software.
 */
#include "buffer.h"

#include <ctype.h>
#include <errno.h>

/**
 * Initialize the provided buffer instance to the indicated size (allocated
 * memory).
 *
 * @param buffer The buffer instance to be initialized.
 * @param size The number of bytes to be preallocated into the buffer.  Note
 *             that a size of zero is allowed but will not allocate a buffer.
 * @return Reference to the internal buffer if successfully allocated or
 *         NULL on a memory allocation failure (if size is non-zero).
 */
uint8_t *WXBuffer_Init(WXBuffer *buffer, size_t size) {
    if (size > 0) {
        buffer->buffer = (uint8_t *) WXMalloc(size);
        if (buffer->buffer == NULL) return NULL;
    } else {
        buffer->buffer = NULL;
    }
    buffer->length = buffer->offset = 0;
    buffer->allocLength = (ssize_t) size;

    return buffer->buffer;
}

/**
 * Initialize the provided buffer instance using a local (alloca) or static
 * data block for the storage.  If required, a data resize will dynamically
 * allocate a new data block so the WXBuffer_Destroy() method should be used
 * to ensure cleanup.
 *
 * @param buffer The buffer instance to be initialized.
 * @param data Reference to the local memory block to use for initialization.
 * @param size The total size (in bytes) of the data block.
 */
void WXBuffer_InitLocal(WXBuffer *buffer, uint8_t *data, size_t size) {
    buffer->buffer = data;
    buffer->length = buffer->offset = 0;
    buffer->allocLength = -((ssize_t) size);
}

/**
 * Reset/empty the contents of the provided buffer (convenience function).
 * Resets the length/offset as though it were a newly allocated instance.
 *
 * @param buffer The buffer instance to be emptied.
 */
void WXBuffer_Empty(WXBuffer *buffer) {
    buffer->length = buffer->offset = 0;
}

/**
 * Resize the buffer if necessary to ensure that the required capacity is
 * available.
 *
 * @param buffer The buffer instance to be (potentially) resized.
 * @param capacity The number of bytes that need to be made available.
 * @param consume If TRUE (non-zero) and a resize is required for capacity,
 *                all information up to the streaming offset will be discarded.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_EnsureCapacity(WXBuffer *buffer, size_t capacity,
                                 int consume) {
    size_t reqLength = buffer->length + capacity;
    size_t allocLength = (buffer->allocLength < 0) ? -buffer->allocLength :
                                                      buffer->allocLength;
    uint8_t *newBuffer;

    if (reqLength > allocLength) {
        /* Attempt to consume first, if allowed */
        if ((consume) && (buffer->offset != 0)) {
            buffer->length -= buffer->offset;
            (void) memmove(buffer->buffer,
                           buffer->buffer + buffer->offset,
                           buffer->length);
            reqLength -= buffer->offset;
            buffer->offset = 0;
        }

        /* Still need more room? */
        if (reqLength > allocLength) {
            allocLength <<= 1;
            if (reqLength > allocLength) allocLength = reqLength + 1;
            newBuffer = (uint8_t *) WXMalloc(allocLength);
            if (newBuffer == NULL) return NULL;
            if (buffer->length != 0) {
                if (buffer->buffer != NULL) {
                    (void) memcpy(newBuffer, buffer->buffer, buffer->length);
                }
            }
            if (buffer->allocLength >= 0) {
                if (buffer->buffer != NULL) {
                    WXFree(buffer->buffer);
                }
            }
            buffer->buffer = newBuffer;
            buffer->allocLength = allocLength;
        }
    }

    return buffer->buffer;
}

/**
 * Append a block of binary data to the contents of the buffer, expanding the
 * internal buffer as necessary.
 *
 * @param buffer The buffer instance to append to.
 * @param data Reference to the block of binary data to be appended.
 * @param length Length (in bytes) of the block to append.
 * @param consume If TRUE (non-zero) and a resize is required for capacity,
 *                all information up to the streaming offset will be discarded.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_Append(WXBuffer *buffer, uint8_t *data, size_t length,
                         int consume) {
    if (WXBuffer_EnsureCapacity(buffer, length, consume) == NULL) return NULL;
    (void) memcpy(buffer->buffer + buffer->length, data, length);
    buffer->length += length;
    return buffer->buffer;
}

/**
 * Append another buffer's content to the specified buffer, expanding the
 * internal buffer as necessary.
 *
 * @param buffer The buffer instance to append to.
 * @param data The buffer to append the contents of.
 * @param consume If TRUE (non-zero) and a resize is required for capacity,
 *                all information up to the streaming offset will be discarded.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_AppendBuffer(WXBuffer *buffer, WXBuffer *source,
                               int consume) {
    size_t len = source->length;
    if (WXBuffer_EnsureCapacity(buffer, len, consume) == NULL) return NULL;
    (void) memcpy(buffer->buffer + buffer->length, source->buffer, len);
    buffer->length += len;
    return buffer->buffer;
}

/**
 * Print a formatted string into the buffer, resizing the buffer as required.
 *
 * @param buffer The buffer instance to print into.
 * @param format The standard printf format string.
 * @param ... The argument set for the printf, according to the format.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_Printf(WXBuffer *buffer, const char *format, ...) {
    ssize_t reqSize;
    va_list ap;
    int len;

    /* WAG, select double format length for initial target buffer size */
    reqSize = (strlen(format) << 1) + 1;

    while (reqSize > 0) {
        /* Create buffer space for target */
        if (WXBuffer_EnsureCapacity(buffer, reqSize,
                                    FALSE) == NULL) return NULL;

        /* Try to print in the allocated space */
        va_start(ap, format);
        len = vsnprintf((char *) (buffer->buffer + buffer->length),
                        reqSize, format, ap);
        va_end(ap);

        /* If conversion was not truncated, all complete */
        if ((len >= 0) && (len < reqSize)) {
            buffer->length += len;
            return buffer->buffer;
        }

        /* Otherwise, determine appropriate size */
        if (len >= 0) {
            /* Standard form, vsnprintf returned actual number of characters */
            reqSize = len + 1;
        } else {
            /* Older printf model, exact size unknown, double until failure */
            reqSize <<= 1;
        }
    }

    /* (Unexpected) overflow condition, indicate failure */
    return NULL;
}

/* Convoluted mechanisms to obtain the wrappers for system endian handling */
/* Note that *all* of these are intended for the unsigned forms! */
#ifndef WIN32
/* One of these must provide htons, htonl, ntohs and ntohl mappings */
#if defined(HAVE_ARPA_INET_H)
#include <arpa/inet.h>
#elif defined(HAVE_NETINET_IN_H)
#include <netinet/in.h>
#endif

#if defined(BYTESWAP_H)
#include <byteswap.h>
#define bswap16(x) bswap_16(x)
#define bswap32(x) bswap_32(x)
#define bswap64(x) bswap_64(x)
#else
#define bswap16(x) \
            ((uint16_t) ((((x) >> 8) & 0xFF) | \
                         (((x) & 0xFF) << 8)))
#define bswap32(x) \
            ((uint32_t) ((((x) & 0xFF000000) >> 24) | \
                         (((x) & 0x00FF0000) >>  8) | \
                         (((x) & 0x0000FF00) <<  8) | \
                         (((x) & 0x000000FF) << 24)))
#define bswap64(x) \
            ((uint64_t) ((((x) & 0xFF00000000000000ull) >> 56) | \
                         (((x) & 0x00FF000000000000ull) >> 40) | \
                         (((x) & 0x0000FF0000000000ull) >> 24) | \
                         (((x) & 0x000000FF00000000ull) >> 8) | \
                         (((x) & 0x00000000FF000000ull) << 8) | \
                         (((x) & 0x0000000000FF0000ull) << 24) | \
                         (((x) & 0x000000000000FF00ull) << 40) | \
                         (((x) & 0x00000000000000FFull) << 56)))
#endif

/* Alas, need to mess around to get 64-bit versions and vax/le-ordering */
#if defined(HAVE_ENDIAN_H)
#include <endian.h>
#define htonll(x) htobe64(x)
#define ntohll(x) be64toh(x)
#elif defined(HAVE_SYS_ENDIAN_H)
#include <sys/endian.h>
/* TODO - need OpenBSD conditions for different naming */
#define htonll(x) htobe64(x)
#define ntohll(x) be64toh(x)
#endif

#else
#include "mstcpip.h"

/* Create the Linux byteswap functions from the Windows forms */
#define bswap16(x) _byteswap_ushort(x)
#define bswap32(x) _byteswap_ulong(x)
#define bswap64(x) _byteswap_uint64(x)
#endif

#define FMT_LITTLE_ENDIAN -1
#define FMT_NATURAL_ENDIAN 0
#define FMT_BIG_ENDIAN 1

#define RPT_VAR_LEN -99999

/* One day this might not be have to be repeated and repeated and repeated */
static char hexchars[] = { '0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

/**
 * Pack a set of values into the buffer according to the (modified) Perl
 * binary pack format.  The packing mechanism recognizes the fixed patterns
 * 'aAbBhHcCsSlLqQnNvVxX', the <> modifiers (not !), the [] and *% length
 * notation and groups ().  Also recognizes z and Z for network and vax ordered
 * 64-bit unsigned values (like nN and vV). And y and Y for base-128 varints of
 * 32-bit and 64-bit length respectively.
 *
 * Note: this is implemented in recursive form for proper group support.
 *
 * @param buffer The buffer instance to pack into.
 * @param format The format to define the packing information.
 * @param ... The argument set for the pack, according to the format.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
static uint8_t *_pack(WXBuffer *buffer, const char *format, size_t flen,
                      va_list *ap, int endian, int isPack) {
    int actvEnd, repeatCount, brCount, grpLen, slen, blen, idx;
    char ch, token, *grpStart, *eptr;
    uint8_t *ptr, bval, hval;
    uint64_t llval;
    uint32_t lval;
    uint16_t sval;

    /* Churn through the format instance, packing as we go */
    while (flen > 0) {
        while ((flen > 0) && isspace(*format)) { format++; flen--; }
        if (flen <= 0) break;
        token = *(format++); flen--;

        /* Groups are special and can be nested */
        if (token == '(') {
            brCount = 1;
            grpStart = (char *) format;
            while (flen > 0) {
                if (*format == '(') brCount++;
                else if (*format == ')') brCount--;
		format++; flen--;
                if (brCount == 0) break;
            }
            if (brCount == 0) {
                grpLen = (format - grpStart) - 1;
            } else {
                /* If we had error notifications, do it here, but just bail */
                break;
            }
        }

        /* Parse token modifiers */
        actvEnd = endian;
        while (flen > 0) {
            while ((flen > 0) && isspace(*format)) { format++; flen--; }
            if (flen <= 0) break;
            if (*format == '<') {
                actvEnd = FMT_LITTLE_ENDIAN;
                format++; flen--;
            } else if (*format == '>') {
                actvEnd = FMT_BIG_ENDIAN;
                format++; flen--;
            } else if (*format == '!') {
                /* Just ignore */
                format++; flen--;
            } else {
                /* Out of formatters */
                break;
            }
        }

        /* Token changes based on forced endian-ness */
        /* Per Perl definitions, sSiIlLqQfd */
        if (actvEnd == FMT_BIG_ENDIAN) {
            switch (token) {
                case 's':
                case 'S':
                    token = 'n';
                    break;
                case 'l':
                case 'L':
                    token = 'N';
                    break;
                case 'q':
                case 'Q':
                    token = 'z';
                    break;
            }
        } else if (actvEnd == FMT_BIG_ENDIAN) {
            switch (token) {
                case 's':
                case 'S':
                    token = 'v';
                    break;
                case 'l':
                case 'L':
                    token = 'V';
                    break;
                case 'q':
                case 'Q':
                    token = 'Z';
                    break;
            }
        }

        /* Repeaters... */
        repeatCount = 1;
        if (flen > 0) {
            if (isdigit(*format)) {
                /* Note, this is ok in group because ) terminates */
                repeatCount = strtol(format, &eptr, 10);
                flen -= eptr - format;
                format = eptr;
            } else if (*format == '[') {
                if (strncmp(format, "[%]", 3) == 0) {
                    repeatCount = va_arg(*ap, int);
                    format += 3; flen -= 3;
                } else if (strncmp(format, "[*]", 3) == 0) {
                    repeatCount = RPT_VAR_LEN;
                    format += 3; flen -= 3;
                } else {
                    /* Like above, but here ] will terminate */
                    repeatCount = strtol(format + 1, &eptr, 10);
                    if (*eptr == ']') {
                        /* Valid count indicator */
                        flen -= (eptr - format) + 1;
                        format = eptr + 1;
                    } else {
                        /* Error if we had it, but it's just garbage now */
                        break;
                    }
                }
            } else if (*format == '%') {
                repeatCount = va_arg(*ap, int);
                format++; flen--;
            } else if (*format == '*') {
                repeatCount = RPT_VAR_LEN;
                format++; flen--;
            }
        }
        if ((repeatCount <= 0) && (repeatCount != RPT_VAR_LEN)) continue;

        /*
         * Note: back and forth here.  Compartmentalize allocated and have
         * common repeat loop?  Compactness against readability?  Went with
         * repeated code in case, could have gone the other way...
         */

        /* Modifiers completed, now process based on token */
        switch(token) {
            /* Group uses recursive calling/parse */
            case '(':
                while (repeatCount > 0) {
                    if (_pack(buffer, grpStart, grpLen, ap,
                              actvEnd, isPack) == NULL) return NULL;
                    repeatCount--;
                }
                break;

            /* Null padded text to specified length */
            case 'a':
            /* Space padded text to specified length */
            case 'A':
                if (isPack) {
                    eptr = va_arg(*ap, char *);
                    slen = strlen(eptr);
                    if (repeatCount == RPT_VAR_LEN) repeatCount = slen;
                    if (WXBuffer_EnsureCapacity(buffer, repeatCount,
                                                TRUE) == NULL) return NULL;
                    if (slen >= repeatCount) {
                        /* Just truncate */
                        (void) memcpy(buffer->buffer + buffer->length,
                                      eptr, repeatCount);
                    } else {
                        /* Extension required */
                        (void) memcpy(buffer->buffer + buffer->length,
                                      eptr, slen);
                        ptr = buffer->buffer + buffer->length + slen;
                        while (slen < repeatCount) {
                            *(ptr++) = (token == 'a') ? '\0' : ' ';
                            slen++;
                        }
                    }
                    buffer->length += repeatCount;
                } else {
                    /* Note: Perl treats a and A unpack identically AFAIK */
                    blen = buffer->length - buffer->offset;
                    if (repeatCount == RPT_VAR_LEN) repeatCount = blen;
                    else if (repeatCount > blen) repeatCount = blen;
                    if ((eptr = WXMalloc(repeatCount + 1)) == NULL) return NULL;
                    (void) memcpy(eptr, buffer->buffer + buffer->offset,
                                  repeatCount);
                    eptr[repeatCount] = '\0';
                    *(va_arg(*ap, char **)) = eptr;
                    buffer->offset += repeatCount;
                }
                break;

            /* Bit string, ascending order */
            case 'b':
            /* Bit string, descending order */
            case 'B':
                if (isPack) {
                    eptr = va_arg(*ap, char *);
                    slen = strlen(eptr);
                    if (repeatCount == RPT_VAR_LEN) repeatCount = slen;
                    else if (slen > repeatCount) slen = repeatCount;

                    blen = (repeatCount + 7) / 8;
                    if (WXBuffer_EnsureCapacity(buffer, blen,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    buffer->length += blen;
                    blen *= 8;
                    for (idx = 0, bval = 0; idx < blen; idx++) {
                        ch = (idx < slen) ? *(eptr++) : '0';
                        bval = (token == 'b') ? (bval >> 1) : (bval << 1);
                        if ((ch & 0x01) != 0) {
                            bval |= (token == 'b') ? 0x80 : 0x01;
                        }
                        if ((idx & 0x07) == 0x07) {
                            *(ptr++) = bval;
                            bval = 0;
                        }
                    }
                } else {
                    blen = (buffer->length - buffer->offset) * 8;
                    if (repeatCount == RPT_VAR_LEN) repeatCount = blen;
                    else if (repeatCount > blen) repeatCount = blen;
                    if ((eptr = WXMalloc(repeatCount + 1)) == NULL) return NULL;
                    bval = *(ptr = buffer->buffer + buffer->offset);
                    *(va_arg(*ap, char **)) = eptr;
                    for (idx = 0; idx < repeatCount; idx++) {
                        if (token == 'b') {
                            *(eptr++) = ((bval & 0x01) != 0) ? '1' : '0';
                            bval = bval >> 1;
                        } else {
                            *(eptr++) = ((bval & 0x80) != 0) ? '1' : '0';
                            bval = bval << 1;
                        }
                        if ((idx & 0x07) == 0x07) bval = *(++ptr);
                    }
                    *eptr = '\0';
                    buffer->offset += (repeatCount + 7) / 8;
                }
                break;

            /* Hex string, low nybble first */
            case 'h':
            /* Hex string, high nybble first */
            case 'H':
                if (isPack) {
                    eptr = va_arg(*ap, char *);
                    slen = strlen(eptr);
                    if (repeatCount == RPT_VAR_LEN) repeatCount = slen;
                    else if (slen > repeatCount) slen = repeatCount;

                    blen = (repeatCount + 1) / 2;
                    if (WXBuffer_EnsureCapacity(buffer, blen,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    buffer->length += blen;
                    blen *= 2;
                    for (idx = 0, bval = 0; idx < blen; idx++) {
                        ch = (idx < slen) ? *(eptr++) : '0';
                        if (isalpha(ch)) hval = (ch + 9) & 0x0F;
                        else hval = ch & 0x0F;
                        if ((idx & 0x01) == 0) {
                            bval |= (token == 'h') ? hval : (hval << 4);
                        } else {
                            bval |= (token == 'h') ? (hval << 4) : hval;
                            *(ptr++) = bval;
                            bval = 0;
                        }
                    }
                } else {
                    blen = (buffer->length - buffer->offset) * 2;
                    if (repeatCount == RPT_VAR_LEN) repeatCount = blen;
                    else if (repeatCount > blen) repeatCount = blen;
                    if ((eptr = WXMalloc(repeatCount + 1)) == NULL) return NULL;
                    bval = *(ptr = buffer->buffer + buffer->offset);
                    *(va_arg(*ap, char **)) = eptr;
                    for (idx = 0; idx < repeatCount; idx++) {
                        if ((token == 'h') || ((idx & 0x01) != 0)) {
                            *(eptr++) = hexchars[bval & 0x0F];
                            bval = bval >> 4;
                        } else {
                            *(eptr++) = hexchars[(bval >> 4) & 0x0F];
                            /* Other half covered by second part of if cond'n */
                        }
                        if ((idx & 0x01) != 0) bval = *(++ptr);
                    }
                    *eptr = '\0';
                    buffer->offset += (repeatCount + 1) / 2;
                }
                break;

            /* Signed/unsigned 8-bit value */
            case 'c':
            case 'C':
                if (isPack) {
                    if (repeatCount == RPT_VAR_LEN) repeatCount = 1;
                    if (WXBuffer_EnsureCapacity(buffer, repeatCount,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    buffer->length += repeatCount;
                    while (repeatCount > 0) {
                        /* Note that char enlarges to int */
                        *(ptr++) = (uint8_t) va_arg(*ap, unsigned int);
                        repeatCount--;
                    }
                } else {
                    blen = buffer->length - buffer->offset;
                    if (repeatCount == RPT_VAR_LEN) repeatCount = blen;
                    else if (repeatCount > blen) repeatCount = blen;
                    ptr = buffer->buffer + buffer->offset;
                    buffer->offset += repeatCount;
                    while (repeatCount > 0) {
                        *(va_arg(*ap, uint8_t *)) = *(ptr++);
                        repeatCount--;
                    }
                }
                break;

            /* Signed/unsigned 16-bit value */
            case 's':
            case 'S':
            /* Network (big-endian) unsigned 16-bit value */
            case 'n':
            /* Vax (little-endian) unsigned 16-bit value */
            case 'v':
                if (isPack) {
                    if (repeatCount == RPT_VAR_LEN) repeatCount = 1;
                    if (WXBuffer_EnsureCapacity(buffer, repeatCount * 2,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    buffer->length += repeatCount * 2;
                    while (repeatCount > 0) {
                        /* Note that short enlarges to int */
                        sval = (uint16_t) va_arg(*ap, unsigned int);
                        if (token == 'n') sval = htons(sval);
                        else if (token == 'v') {
                            sval = htons(sval);
                            sval = bswap16(sval);
                        }
                        *((uint16_t *) ptr) = sval;
                        ptr += 2;
                        repeatCount--;
                    }
                } else {
                    blen = (buffer->length - buffer->offset) / 2;
                    if (repeatCount == RPT_VAR_LEN) repeatCount = blen;
                    else if (repeatCount > blen) repeatCount = blen;
                    ptr = buffer->buffer + buffer->offset;
                    buffer->offset += repeatCount * 2;
                    while (repeatCount > 0) {
                        sval = *((uint16_t *) ptr);
                        if (token == 'n') sval = ntohs(sval);
                        else if (token == 'v') {
                            sval = ntohs(sval);
                            sval = bswap16(sval);
                        }
                        *(va_arg(*ap, uint16_t *)) = sval;
                        ptr += 2;
                        repeatCount--;
                    }
                }
                break;

            /* Signed/unsigned 32-bit value */
            case 'l':
            case 'L':
            /* Network (big-endian) unsigned 32-bit value */
            case 'N':
            /* Vax (little-endian) unsigned 32-bit value */
            case 'V':
                if (isPack) {
                    if (repeatCount == RPT_VAR_LEN) repeatCount = 1;
                    if (WXBuffer_EnsureCapacity(buffer, repeatCount * 4,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    buffer->length += repeatCount * 4;
                    while (repeatCount > 0) {
                        lval = (uint32_t) va_arg(*ap, uint32_t);
                        if (token == 'N') lval = htonl(lval);
                        else if (token == 'V') {
                            lval = htonl(lval);
                            lval = bswap32(lval);
                        }
                        *((uint32_t *) ptr) = lval;
                        ptr += 4;
                        repeatCount--;
                    }
                } else {
                    blen = (buffer->length - buffer->offset) / 4;
                    if (repeatCount == RPT_VAR_LEN) repeatCount = blen;
                    else if (repeatCount > blen) repeatCount = blen;
                    ptr = buffer->buffer + buffer->offset;
                    buffer->offset += repeatCount * 4;
                    while (repeatCount > 0) {
                        lval = *((uint32_t *) ptr);
                        if (token == 'N') lval = ntohl(lval);
                        else if (token == 'V') {
                            lval = ntohl(lval);
                            lval = bswap32(lval);
                        }
                        *(va_arg(*ap, uint32_t *)) = lval;
                        ptr += 4;
                        repeatCount--;
                    }
                }
                break;

            /* Signed/unsigned 64-bit value */
            case 'q':
            case 'Q':
            /* Network (big-endian) unsigned 64-bit value */
            case 'z':
            /* Vax (little-endian) unsigned 64-bit value */
            case 'Z':
                if (isPack) {
                    if (repeatCount == RPT_VAR_LEN) repeatCount = 1;
                    if (WXBuffer_EnsureCapacity(buffer, repeatCount * 8,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    buffer->length += repeatCount * 8;
                    while (repeatCount > 0) {
                        llval = (uint64_t) va_arg(*ap, uint64_t);
                        if (token == 'z') llval = htonll(llval);
                        else if (token == 'Z') {
                            llval = htonll(llval);
                            llval = bswap64(llval);
                        }
                        *((uint64_t *) ptr) = llval;
                        ptr += 4;
                        repeatCount--;
                    }
                } else {
                    blen = (buffer->length - buffer->offset) / 8;
                    if (repeatCount == RPT_VAR_LEN) repeatCount = blen;
                    else if (repeatCount > blen) repeatCount = blen;
                    ptr = buffer->buffer + buffer->offset;
                    buffer->offset += repeatCount * 8;
                    while (repeatCount > 0) {
                        llval = *((uint64_t *) ptr);
                        if (token == 'z') llval = htonll(llval);
                        else if (token == 'Z') {
                            llval = ntohll(llval);
                            llval = bswap64(llval);
                        }
                        *(va_arg(*ap, uint64_t *)) = llval;
                        ptr += 8;
                        repeatCount--;
                    }
                }
                break;

            /* Null byte (0x0) */
            case 'x':
                if (repeatCount == RPT_VAR_LEN) repeatCount = 1;
                if (isPack) {
                    if (WXBuffer_EnsureCapacity(buffer, repeatCount,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    buffer->length += repeatCount;
                    while (repeatCount > 0) {
                        *(ptr++) = 0x0;
                        repeatCount--;
                    }
                } else {
                    if (buffer->offset + repeatCount > buffer->length) {
                        /* Out of room */
                        return NULL;
                    }
                    buffer->offset += repeatCount;
                }
                break;

            /* Reverse a byte in the buffer */
            case 'X':
                if (repeatCount == RPT_VAR_LEN) repeatCount = 1;
                /* This is a lot easier.. */
                if (isPack) {
                    if (repeatCount > (int) buffer->length) {
                        buffer->length = 0;
                    } else {
                        buffer->length -= repeatCount;
                    }
                } else {
                    if (repeatCount > (int) buffer->offset) {
                        buffer->offset = 0;
                    } else {
                        buffer->offset -= repeatCount;
                    }
                }
                break;

            /* Handle 32-bit and 64-bit varints */
            case 'y':
            case 'Y':
                if (isPack) {
                    if (repeatCount == RPT_VAR_LEN) repeatCount = 1;
                    if (WXBuffer_EnsureCapacity(buffer, repeatCount * 10,
                                                TRUE) == NULL) return NULL;
                    ptr = buffer->buffer + buffer->length;
                    while (repeatCount > 0) {
                        if (token == 'y') {
                            llval = (uint64_t) va_arg(*ap, uint32_t);
                        } else {
                            llval = (uint64_t) va_arg(*ap, uint64_t);
                        }
                        if (llval == 0) {
                            *(ptr++) = 0x0;
                            buffer->length++;
                        } else {
                            while (llval != 0) {
                                bval = (uint8_t) (llval & 0x7F);
                                llval = llval >> 7;
                                if (llval != 0) bval |= 0x80;
                                *(ptr++) = bval;
                                buffer->length++;
                            }
                        }
                        repeatCount--;
                    }
                } else {
                    llval = idx = 0;
                    ptr = buffer->buffer + buffer->offset;
                    while (((repeatCount > 0) ||
                                      (repeatCount == RPT_VAR_LEN)) &&
                               (buffer->offset < buffer->length)) {
                        bval = *(ptr++);
                        buffer->offset++;
                        llval |= (bval & 0x7F) << idx;
                        idx += 7;
                        if ((bval & 0x80) == 0) {
                            if (token == 'y') {
                                *(va_arg(*ap, uint32_t *)) = (uint32_t) llval;
                            } else {
                                *(va_arg(*ap, uint64_t *)) = llval;
                            }
                            llval = idx = 0;
                            if (repeatCount > 0) repeatCount--;
                        }
                    }
                }
                break;
        }
    }

    /* Survived the outcome, return the content */
    return buffer->buffer;
}

uint8_t *WXBuffer_Pack(WXBuffer *buffer, const char *format, ...) {
    uint8_t *content;
    va_list ap;

    /* Just initiate the vararg set and jump to the recursion method */
    va_start(ap, format);
    content = _pack(buffer, format, strlen(format), &ap,
                    FMT_NATURAL_ENDIAN, TRUE);
    va_end(ap);

    return content;
}

/**
 * Identical to the above, but pack based on an explicit varargs instance.
 *
 * @param buffer The buffer instance to pack into.
 * @param format The format to define the packing information.
 * @param ap The allocated vararg instance.  Note that the state of this
 *           is indeterminant after the call.
 * @return Reference to the internal buffer if successfully (re)allocated or
 *         NULL on a memory allocation failure.
 */
uint8_t *WXBuffer_VPack(WXBuffer *buffer, const char *format, va_list ap) {
    return _pack(buffer, format, strlen(format), (void *) &ap,
                 FMT_NATURAL_ENDIAN, TRUE);
}

/**
 * Unpack a set of values into the buffer according to the (modified) Perl
 * binary pack format.  The packing mechanism recognizes the patterns
 * 'aAbBhHcCsSlLqQnNvVxX', the <> modifiers (not !), the [] and *% length
 * notation and groups (), along with the zZ and yY extensions as described
 * in the Pack() method.
 *
 * @param buffer The buffer instance to unpack from.
 * @param format The format to define the packing information.
 * @param ... The argument set for the unpack, according to the format.
 * @return Reference to the internal buffer if successfully parsed or
 *         NULL on a memory allocation failure or packing error.
 */
uint8_t *WXBuffer_Unpack(WXBuffer *buffer, const char *format, ...) {
    uint8_t *content;
    va_list ap;

    /* Just initiate the vararg set and jump to the recursion method */
    va_start(ap, format);
    content = _pack(buffer, format, strlen(format), &ap,
                    FMT_NATURAL_ENDIAN, FALSE);
    va_end(ap);

    return content;
}

/**
 * Identical to the above, but unpack based on an explicit varargs instance.
 *
 * @param buffer The buffer instance to unpack from.
 * @param format The format to define the packing information.
 * @param ap The allocated vararg instance.  Note that the state of this
 *           is indeterminant after the call.
 * @return Reference to the internal buffer if successfully parsed or
 *         NULL on a memory allocation failure or packing error.
 */
uint8_t *WXBuffer_VUnpack(WXBuffer *buffer, const char *format, va_list ap) {
    return _pack(buffer, format, strlen(format), (void *) &ap,
                 FMT_NATURAL_ENDIAN, FALSE);
}

/**
 * Read the contents of the provided file descriptor into the buffer
 * (appended to the end of the buffer).
 *
 * @param buffer The buffer instance to read into.
 * @param fd The file descriptor to read from.
 * @param length The (maximum) number of bytes to read from the file, zero
 *               to read the remainder.
 * @return The number of bytes read from the file, -1 on error (partial
 *         contents may be read).
 */
ssize_t WXBuffer_ReadFile(WXBuffer *buffer, int fd, size_t length) {
    ssize_t len, count = 0;
    size_t block = 8192;
    uint8_t *ptr;

    if (length == 0) length = (size_t) -1;
    while (length > 0) {
        /* Attempt to allocate and read a (remaining) chunk */
        if (length < block) block = length;
        if (WXBuffer_EnsureCapacity(buffer, block, TRUE) == NULL) return -1;
        ptr = buffer->buffer + buffer->length;
#ifdef WIN32
        len = _read(fd, ptr, block);
#else
        len = read(fd, ptr, block);
#endif

        /* Handle the result appropriately */
        if (len < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) continue;
            return -1;
        }
        if (len == 0) break;
        count += len;
        buffer->length += len;
        length -= len;
    }

    return count;
}

/**
 * Write the contents of the buffer to the provided file descriptor, starting
 * from the current buffer offset (which will be adjusted accordingly).
 *
 * @param buffer The buffer instance to write from.
 * @param fd The file descriptor to read to.
 * @return The number of bytes written to the file, -1 on error (partial
 *         contents may be written).
 */
ssize_t WXBuffer_WriteFile(WXBuffer *buffer, int fd) {
    ssize_t len, count = 0, length = buffer->length - buffer->offset;
    uint8_t *ptr = buffer->buffer + buffer->offset;

    while (length > 0) {
        /* Attempt/issue the remainder write */
#ifdef WIN32
        len = _write(fd, ptr, length);
#else
        len = write(fd, ptr, length);
#endif

        /* Handle the result appropriately */
        if (len < 0) {
            if ((errno == EAGAIN) || (errno == EINTR)) continue;
            return -1;
        }
        count += len;
        buffer->offset += len;
        length -= len;
    }

    return count;
}

/**
 * Destroy (free) the contents of the provided buffer.  This does not free
 * the buffer structure itself, only the allocated content.  For locally
 * allocated buffers, only dereferences the local storage block.
 *
 * @param buffer The buffer instance to destroy.
 */
void WXBuffer_Destroy(WXBuffer *buffer) {
    if (buffer->allocLength >= 0) {
        WXFree(buffer->buffer);
    }
    buffer->buffer = NULL;
    buffer->allocLength = 0;
    buffer->offset = buffer->length = 0;
}
