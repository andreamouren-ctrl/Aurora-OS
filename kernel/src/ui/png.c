#include <stddef.h>
#include <stdint.h>

#include <aurora/heap.h>
#include <aurora/png.h>

#define PNG_SIGNATURE_SIZE 8u
#define PNG_MAX_HUFFMAN_BITS 15u
#define PNG_MAX_LITLEN_SYMBOLS 288u
#define PNG_MAX_DIST_SYMBOLS 32u
#define PNG_MAX_CODELEN_SYMBOLS 19u
#define PNG_MAX_DYNAMIC_SYMBOLS 320u

struct bit_reader {
    const uint8_t *data;
    size_t size;
    size_t byte_pos;
    uint8_t bit_pos;
    bool failed;
};

struct huffman {
    uint16_t count[PNG_MAX_HUFFMAN_BITS + 1u];
    uint16_t symbol[PNG_MAX_DYNAMIC_SYMBOLS];
    uint16_t symbol_count;
};

static const uint8_t png_signature[PNG_SIGNATURE_SIZE] = {
    137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
};

static void clear_bytes(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static void copy_bytes(void *destination, const void *source, size_t size) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    if (destination == NULL || source == NULL) return;
    for (size_t i = 0u; i < size; ++i) dst[i] = src[i];
}

static bool bytes_equal(const void *left, const void *right, size_t size) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    if (left == NULL || right == NULL) return false;
    uint8_t diff = 0u;
    for (size_t i = 0u; i < size; ++i) diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0u;
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    for (size_t i = 0u; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return crc;
}

static bool chunk_crc_valid(
    const uint8_t type[4],
    const uint8_t *payload,
    size_t payload_size,
    uint32_t expected
) {
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    crc = crc32_update(crc, type, 4u);
    crc = crc32_update(crc, payload, payload_size);
    return (crc ^ UINT32_C(0xFFFFFFFF)) == expected;
}

static uint32_t bits_read(struct bit_reader *reader, uint32_t count) {
    if (reader == NULL || count > 24u) return 0u;
    uint32_t value = 0u;
    for (uint32_t i = 0u; i < count; ++i) {
        if (reader->byte_pos >= reader->size) {
            reader->failed = true;
            return 0u;
        }
        uint32_t bit =
            (uint32_t)((reader->data[reader->byte_pos] >> reader->bit_pos) & 1u);
        value |= bit << i;
        ++reader->bit_pos;
        if (reader->bit_pos == 8u) {
            reader->bit_pos = 0u;
            ++reader->byte_pos;
        }
    }
    return value;
}

static void bits_align_byte(struct bit_reader *reader) {
    if (reader == NULL) return;
    if (reader->bit_pos != 0u) {
        reader->bit_pos = 0u;
        ++reader->byte_pos;
        if (reader->byte_pos > reader->size) reader->failed = true;
    }
}

static bool huffman_build(
    struct huffman *table,
    const uint8_t *lengths,
    uint16_t symbol_count
) {
    if (table == NULL || lengths == NULL ||
        symbol_count == 0u || symbol_count > PNG_MAX_DYNAMIC_SYMBOLS) {
        return false;
    }

    clear_bytes(table, sizeof(*table));
    table->symbol_count = symbol_count;

    for (uint16_t symbol = 0u; symbol < symbol_count; ++symbol) {
        uint8_t length = lengths[symbol];
        if (length > PNG_MAX_HUFFMAN_BITS) return false;
        ++table->count[length];
    }

    if (table->count[0] == symbol_count) return false;

    int32_t left = 1;
    for (uint32_t length = 1u; length <= PNG_MAX_HUFFMAN_BITS; ++length) {
        left <<= 1;
        left -= table->count[length];
        if (left < 0) return false;
    }

    uint16_t offsets[PNG_MAX_HUFFMAN_BITS + 1u];
    clear_bytes(offsets, sizeof(offsets));
    offsets[1] = 0u;
    for (uint32_t length = 1u; length < PNG_MAX_HUFFMAN_BITS; ++length) {
        offsets[length + 1u] =
            (uint16_t)(offsets[length] + table->count[length]);
    }

    for (uint16_t symbol = 0u; symbol < symbol_count; ++symbol) {
        uint8_t length = lengths[symbol];
        if (length != 0u) table->symbol[offsets[length]++] = symbol;
    }
    return true;
}

static int32_t huffman_decode(
    struct bit_reader *reader,
    const struct huffman *table
) {
    if (reader == NULL || table == NULL) return -1;

    uint32_t code = 0u;
    uint32_t first = 0u;
    uint32_t index = 0u;

    for (uint32_t length = 1u; length <= PNG_MAX_HUFFMAN_BITS; ++length) {
        code |= bits_read(reader, 1u);
        if (reader->failed) return -1;

        uint32_t count = table->count[length];
        if (code < first + count) {
            uint32_t offset = index + (code - first);
            if (offset >= table->symbol_count) return -1;
            return (int32_t)table->symbol[offset];
        }

        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }

    return -1;
}

static bool build_fixed_tables(
    struct huffman *litlen,
    struct huffman *dist
) {
    uint8_t litlen_lengths[PNG_MAX_LITLEN_SYMBOLS];
    uint8_t dist_lengths[PNG_MAX_DIST_SYMBOLS];

    for (uint32_t i = 0u; i <= 143u; ++i) litlen_lengths[i] = 8u;
    for (uint32_t i = 144u; i <= 255u; ++i) litlen_lengths[i] = 9u;
    for (uint32_t i = 256u; i <= 279u; ++i) litlen_lengths[i] = 7u;
    for (uint32_t i = 280u; i < PNG_MAX_LITLEN_SYMBOLS; ++i) litlen_lengths[i] = 8u;
    for (uint32_t i = 0u; i < PNG_MAX_DIST_SYMBOLS; ++i) dist_lengths[i] = 5u;

    return huffman_build(litlen, litlen_lengths, PNG_MAX_LITLEN_SYMBOLS) &&
           huffman_build(dist, dist_lengths, PNG_MAX_DIST_SYMBOLS);
}

static bool build_dynamic_tables(
    struct bit_reader *reader,
    struct huffman *litlen,
    struct huffman *dist
) {
    static const uint8_t order[PNG_MAX_CODELEN_SYMBOLS] = {
        16u, 17u, 18u, 0u, 8u, 7u, 9u, 6u, 10u, 5u,
        11u, 4u, 12u, 3u, 13u, 2u, 14u, 1u, 15u
    };

    uint32_t hlit = bits_read(reader, 5u) + 257u;
    uint32_t hdist = bits_read(reader, 5u) + 1u;
    uint32_t hclen = bits_read(reader, 4u) + 4u;
    if (reader->failed || hlit > 286u || hdist > 32u || hclen > 19u) return false;

    uint8_t code_lengths[PNG_MAX_CODELEN_SYMBOLS];
    clear_bytes(code_lengths, sizeof(code_lengths));
    for (uint32_t i = 0u; i < hclen; ++i) {
        code_lengths[order[i]] = (uint8_t)bits_read(reader, 3u);
        if (reader->failed) return false;
    }

    struct huffman code_table;
    if (!huffman_build(&code_table, code_lengths, PNG_MAX_CODELEN_SYMBOLS)) return false;

    uint8_t lengths[PNG_MAX_DYNAMIC_SYMBOLS];
    clear_bytes(lengths, sizeof(lengths));
    uint32_t total = hlit + hdist;
    uint32_t index = 0u;

    while (index < total) {
        int32_t symbol = huffman_decode(reader, &code_table);
        if (symbol < 0) return false;

        if (symbol <= 15) {
            lengths[index++] = (uint8_t)symbol;
        } else if (symbol == 16) {
            if (index == 0u) return false;
            uint32_t repeat = bits_read(reader, 2u) + 3u;
            if (reader->failed || index + repeat > total) return false;
            uint8_t previous = lengths[index - 1u];
            while (repeat-- != 0u) lengths[index++] = previous;
        } else if (symbol == 17) {
            uint32_t repeat = bits_read(reader, 3u) + 3u;
            if (reader->failed || index + repeat > total) return false;
            while (repeat-- != 0u) lengths[index++] = 0u;
        } else if (symbol == 18) {
            uint32_t repeat = bits_read(reader, 7u) + 11u;
            if (reader->failed || index + repeat > total) return false;
            while (repeat-- != 0u) lengths[index++] = 0u;
        } else {
            return false;
        }
    }

    if (lengths[256] == 0u) return false;
    return huffman_build(litlen, lengths, (uint16_t)hlit) &&
           huffman_build(dist, lengths + hlit, (uint16_t)hdist);
}

static bool inflate_codes(
    struct bit_reader *reader,
    const struct huffman *litlen,
    const struct huffman *dist,
    uint8_t *output,
    size_t output_size,
    size_t *inout_offset
) {
    static const uint16_t length_base[29] = {
        3u,4u,5u,6u,7u,8u,9u,10u,11u,13u,15u,17u,19u,23u,27u,
        31u,35u,43u,51u,59u,67u,83u,99u,115u,131u,163u,195u,227u,258u
    };
    static const uint8_t length_extra[29] = {
        0u,0u,0u,0u,0u,0u,0u,0u,1u,1u,1u,1u,2u,2u,2u,2u,3u,3u,
        3u,3u,4u,4u,4u,4u,5u,5u,5u,5u,0u
    };
    static const uint16_t dist_base[30] = {
        1u,2u,3u,4u,5u,7u,9u,13u,17u,25u,33u,49u,65u,97u,129u,
        193u,257u,385u,513u,769u,1025u,1537u,2049u,3073u,4097u,
        6145u,8193u,12289u,16385u,24577u
    };
    static const uint8_t dist_extra[30] = {
        0u,0u,0u,0u,1u,1u,2u,2u,3u,3u,4u,4u,5u,5u,6u,6u,7u,7u,
        8u,8u,9u,9u,10u,10u,11u,11u,12u,12u,13u,13u
    };

    size_t out = *inout_offset;
    for (;;) {
        int32_t symbol = huffman_decode(reader, litlen);
        if (symbol < 0) return false;

        if (symbol < 256) {
            if (out >= output_size) return false;
            output[out++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256) {
            *inout_offset = out;
            return true;
        }
        if (symbol < 257 || symbol > 285) return false;

        uint32_t li = (uint32_t)symbol - 257u;
        uint32_t length = length_base[li] + bits_read(reader, length_extra[li]);
        if (reader->failed) return false;

        int32_t dsymbol = huffman_decode(reader, dist);
        if (dsymbol < 0 || dsymbol > 29) return false;
        uint32_t distance = dist_base[dsymbol] +
            bits_read(reader, dist_extra[dsymbol]);
        if (reader->failed || distance == 0u || distance > out) return false;
        if ((size_t)length > output_size - out) return false;

        for (uint32_t i = 0u; i < length; ++i) {
            output[out] = output[out - distance];
            ++out;
        }
    }
}

static bool zlib_inflate_exact(
    const uint8_t *input,
    size_t input_size,
    uint8_t *output,
    size_t output_size
) {
    if (input == NULL || output == NULL || input_size < 6u) return false;

    uint8_t cmf = input[0];
    uint8_t flg = input[1];
    if ((cmf & 0x0Fu) != 8u || (cmf >> 4) > 7u ||
        ((((uint32_t)cmf << 8) | flg) % 31u) != 0u ||
        (flg & 0x20u) != 0u) {
        return false;
    }

    struct bit_reader reader = {
        .data = input + 2u,
        .size = input_size - 6u,
        .byte_pos = 0u,
        .bit_pos = 0u,
        .failed = false
    };

    size_t out = 0u;
    bool final_block = false;
    while (!final_block) {
        final_block = bits_read(&reader, 1u) != 0u;
        uint32_t block_type = bits_read(&reader, 2u);
        if (reader.failed) return false;

        if (block_type == 0u) {
            bits_align_byte(&reader);
            if (reader.failed || reader.byte_pos + 4u > reader.size) return false;
            uint16_t length = (uint16_t)(
                reader.data[reader.byte_pos] |
                ((uint16_t)reader.data[reader.byte_pos + 1u] << 8));
            uint16_t inverse = (uint16_t)(
                reader.data[reader.byte_pos + 2u] |
                ((uint16_t)reader.data[reader.byte_pos + 3u] << 8));
            reader.byte_pos += 4u;
            if ((uint16_t)(length ^ UINT16_C(0xFFFF)) != inverse ||
                reader.byte_pos + length > reader.size ||
                (size_t)length > output_size - out) {
                return false;
            }
            copy_bytes(output + out, reader.data + reader.byte_pos, length);
            reader.byte_pos += length;
            out += length;
        } else if (block_type == 1u || block_type == 2u) {
            struct huffman litlen;
            struct huffman dist;
            bool ok = block_type == 1u
                ? build_fixed_tables(&litlen, &dist)
                : build_dynamic_tables(&reader, &litlen, &dist);
            if (!ok || !inflate_codes(
                    &reader, &litlen, &dist, output, output_size, &out)) {
                return false;
            }
        } else {
            return false;
        }
    }

    if (out != output_size) return false;

    uint32_t s1 = 1u;
    uint32_t s2 = 0u;
    for (size_t i = 0u; i < output_size; ++i) {
        s1 = (s1 + output[i]) % 65521u;
        s2 = (s2 + s1) % 65521u;
    }
    uint32_t actual_adler = (s2 << 16) | s1;
    uint32_t expected_adler = read_be32(input + input_size - 4u);
    return actual_adler == expected_adler;
}

static uint8_t paeth_predictor(uint8_t left, uint8_t above, uint8_t upper_left) {
    int32_t p = (int32_t)left + (int32_t)above - (int32_t)upper_left;
    int32_t pa = p - (int32_t)left;
    int32_t pb = p - (int32_t)above;
    int32_t pc = p - (int32_t)upper_left;
    if (pa < 0) pa = -pa;
    if (pb < 0) pb = -pb;
    if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return left;
    if (pb <= pc) return above;
    return upper_left;
}

static bool unfilter_scanlines(
    uint8_t *raw,
    uint32_t width,
    uint32_t height,
    uint32_t bytes_per_pixel
) {
    size_t row_bytes = (size_t)width * bytes_per_pixel;
    size_t stride = row_bytes + 1u;

    for (uint32_t y = 0u; y < height; ++y) {
        uint8_t *row = raw + (size_t)y * stride;
        uint8_t filter = row[0];
        uint8_t *pixels = row + 1u;
        const uint8_t *previous = y == 0u ? NULL : row - stride + 1u;

        for (size_t x = 0u; x < row_bytes; ++x) {
            uint8_t left = x >= bytes_per_pixel ? pixels[x - bytes_per_pixel] : 0u;
            uint8_t above = previous != NULL ? previous[x] : 0u;
            uint8_t upper_left =
                previous != NULL && x >= bytes_per_pixel
                    ? previous[x - bytes_per_pixel]
                    : 0u;

            switch (filter) {
                case 0u:
                    break;
                case 1u:
                    pixels[x] = (uint8_t)(pixels[x] + left);
                    break;
                case 2u:
                    pixels[x] = (uint8_t)(pixels[x] + above);
                    break;
                case 3u:
                    pixels[x] = (uint8_t)(pixels[x] +
                        (uint8_t)(((uint32_t)left + above) / 2u));
                    break;
                case 4u:
                    pixels[x] = (uint8_t)(pixels[x] +
                        paeth_predictor(left, above, upper_left));
                    break;
                default:
                    return false;
            }
        }
    }
    return true;
}

bool aurora_png_decode(
    const uint8_t *data,
    size_t size,
    struct aurora_png_image *out_image
) {
    if (data == NULL || out_image == NULL || size < 33u ||
        !bytes_equal(data, png_signature, PNG_SIGNATURE_SIZE)) {
        return false;
    }

    clear_bytes(out_image, sizeof(*out_image));

    uint32_t width = 0u;
    uint32_t height = 0u;
    uint8_t color_type = 0u;
    uint32_t bytes_per_pixel = 0u;
    bool seen_ihdr = false;
    bool seen_iend = false;
    size_t idat_size = 0u;

    size_t offset = PNG_SIGNATURE_SIZE;
    while (offset + 12u <= size) {
        uint32_t chunk_length = read_be32(data + offset);
        if ((size_t)chunk_length > size - offset - 12u) return false;
        const uint8_t *type = data + offset + 4u;
        const uint8_t *payload = data + offset + 8u;
        uint32_t crc = read_be32(payload + chunk_length);
        if (!chunk_crc_valid(type, payload, chunk_length, crc)) return false;

        if (bytes_equal(type, "IHDR", 4u)) {
            if (seen_ihdr || chunk_length != 13u) return false;
            width = read_be32(payload);
            height = read_be32(payload + 4u);
            uint8_t bit_depth = payload[8];
            color_type = payload[9];
            if (width == 0u || height == 0u || bit_depth != 8u ||
                payload[10] != 0u || payload[11] != 0u || payload[12] != 0u ||
                (color_type != 2u && color_type != 6u)) {
                return false;
            }
            bytes_per_pixel = color_type == 2u ? 3u : 4u;
            seen_ihdr = true;
        } else if (bytes_equal(type, "IDAT", 4u)) {
            if (!seen_ihdr || seen_iend ||
                idat_size > (size_t)-1 - chunk_length) return false;
            idat_size += chunk_length;
        } else if (bytes_equal(type, "IEND", 4u)) {
            if (!seen_ihdr || chunk_length != 0u) return false;
            seen_iend = true;
            break;
        }

        offset += 12u + chunk_length;
    }

    if (!seen_ihdr || !seen_iend || idat_size == 0u) return false;

    if ((size_t)width > ((size_t)-1) / bytes_per_pixel) return false;
    size_t row_bytes = (size_t)width * bytes_per_pixel;
    if (row_bytes == (size_t)-1 ||
        (size_t)height > ((size_t)-1) / (row_bytes + 1u)) return false;
    size_t raw_size = (row_bytes + 1u) * (size_t)height;

    if ((size_t)width > ((size_t)-1) / 4u ||
        (size_t)height > ((size_t)-1) / ((size_t)width * 4u)) return false;
    size_t rgba_size = (size_t)width * (size_t)height * 4u;

    uint8_t *compressed = (uint8_t *)kheap_alloc(idat_size, 16u);
    uint8_t *raw = (uint8_t *)kheap_alloc(raw_size, 16u);
    uint8_t *rgba = (uint8_t *)kheap_alloc(rgba_size, 16u);
    if (compressed == NULL || raw == NULL || rgba == NULL) {
        if (compressed != NULL) (void)kheap_free_sized(compressed, idat_size);
        if (raw != NULL) (void)kheap_free_sized(raw, raw_size);
        if (rgba != NULL) (void)kheap_free_sized(rgba, rgba_size);
        return false;
    }

    size_t write = 0u;
    offset = PNG_SIGNATURE_SIZE;
    while (offset + 12u <= size) {
        uint32_t chunk_length = read_be32(data + offset);
        const uint8_t *type = data + offset + 4u;
        const uint8_t *payload = data + offset + 8u;
        if (bytes_equal(type, "IDAT", 4u)) {
            copy_bytes(compressed + write, payload, chunk_length);
            write += chunk_length;
        } else if (bytes_equal(type, "IEND", 4u)) {
            break;
        }
        offset += 12u + chunk_length;
    }

    bool ok = write == idat_size &&
        zlib_inflate_exact(compressed, idat_size, raw, raw_size) &&
        unfilter_scanlines(raw, width, height, bytes_per_pixel);

    if (ok) {
        size_t stride = row_bytes + 1u;
        size_t out = 0u;
        for (uint32_t y = 0u; y < height; ++y) {
            const uint8_t *row = raw + (size_t)y * stride + 1u;
            for (uint32_t x = 0u; x < width; ++x) {
                const uint8_t *pixel = row + (size_t)x * bytes_per_pixel;
                rgba[out++] = pixel[0];
                rgba[out++] = pixel[1];
                rgba[out++] = pixel[2];
                rgba[out++] = color_type == 6u ? pixel[3] : 255u;
            }
        }
    }

    (void)kheap_free_sized(compressed, idat_size);
    (void)kheap_free_sized(raw, raw_size);

    if (!ok) {
        (void)kheap_free_sized(rgba, rgba_size);
        return false;
    }

    out_image->width = width;
    out_image->height = height;
    out_image->rgba = rgba;
    out_image->rgba_size = rgba_size;
    return true;
}

void aurora_png_release(struct aurora_png_image *image) {
    if (image == NULL) return;
    if (image->rgba != NULL && image->rgba_size != 0u) {
        (void)kheap_free_sized(image->rgba, image->rgba_size);
    }
    clear_bytes(image, sizeof(*image));
}
