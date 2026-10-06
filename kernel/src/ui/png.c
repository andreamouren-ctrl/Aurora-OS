#include <stddef.h>
#include <stdint.h>

#include <aurora/heap.h>
#include <aurora/png.h>

#define PNG_SIGNATURE_SIZE 8u
#define PNG_HUFFMAN_BITS 15u
#define PNG_HUFFMAN_TABLE_SIZE (1u << PNG_HUFFMAN_BITS)
#define PNG_CODELEN_BITS 7u
#define PNG_CODELEN_TABLE_SIZE (1u << PNG_CODELEN_BITS)

struct png_huffman_entry {
    uint16_t symbol;
    uint8_t bits;
    uint8_t valid;
};

struct png_bit_reader {
    const uint8_t *data;
    size_t size;
    size_t offset;
    uint64_t bit_buffer;
    uint32_t bit_count;
};

static void png_clear(void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    if (buffer == NULL) return;
    for (size_t i = 0u; i < size; ++i) bytes[i] = 0u;
}

static void png_copy(void *destination, const void *source, size_t size) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    if (destination == NULL || source == NULL) return;
    for (size_t i = 0u; i < size; ++i) dst[i] = src[i];
}

static uint32_t png_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint32_t png_crc32_update(uint32_t crc, const uint8_t *data, size_t size) {
    for (size_t i = 0u; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return crc;
}

static bool png_chunk_crc_ok(
    const uint8_t type[4],
    const uint8_t *payload,
    size_t payload_size,
    uint32_t expected
) {
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    crc = png_crc32_update(crc, type, 4u);
    crc = png_crc32_update(crc, payload, payload_size);
    crc ^= UINT32_C(0xFFFFFFFF);
    return crc == expected;
}

static uint32_t png_adler32(const uint8_t *data, size_t size) {
    uint32_t a = 1u;
    uint32_t b = 0u;
    for (size_t i = 0u; i < size; ++i) {
        a += data[i];
        if (a >= 65521u) a -= 65521u;
        b += a;
        if (b >= 65521u) b %= 65521u;
    }
    return (b << 16) | a;
}

static bool png_bits_fill(struct png_bit_reader *reader, uint32_t wanted) {
    if (reader == NULL || wanted > 32u) return false;
    while (reader->bit_count < wanted && reader->offset < reader->size) {
        reader->bit_buffer |=
            (uint64_t)reader->data[reader->offset++] << reader->bit_count;
        reader->bit_count += 8u;
    }
    return reader->bit_count >= wanted;
}

static bool png_bits_read(
    struct png_bit_reader *reader,
    uint32_t count,
    uint32_t *out
) {
    if (reader == NULL || out == NULL || count > 24u) return false;
    if (count == 0u) {
        *out = 0u;
        return true;
    }
    if (!png_bits_fill(reader, count)) return false;
    uint64_t mask = (UINT64_C(1) << count) - 1u;
    *out = (uint32_t)(reader->bit_buffer & mask);
    reader->bit_buffer >>= count;
    reader->bit_count -= count;
    return true;
}

static void png_bits_align_byte(struct png_bit_reader *reader) {
    if (reader == NULL) return;
    uint32_t discard = reader->bit_count & 7u;
    reader->bit_buffer >>= discard;
    reader->bit_count -= discard;
}

static uint16_t png_reverse_bits(uint16_t code, uint8_t length) {
    uint16_t reversed = 0u;
    for (uint8_t i = 0u; i < length; ++i) {
        reversed = (uint16_t)((reversed << 1) | (code & 1u));
        code = (uint16_t)(code >> 1);
    }
    return reversed;
}

static bool png_build_huffman(
    const uint8_t *lengths,
    uint16_t symbol_count,
    uint8_t max_bits,
    struct png_huffman_entry *table,
    size_t table_count
) {
    uint16_t counts[16];
    uint16_t next_code[16];
    png_clear(counts, sizeof(counts));
    png_clear(next_code, sizeof(next_code));
    if (lengths == NULL || table == NULL || max_bits == 0u ||
        max_bits > 15u || table_count != ((size_t)1u << max_bits)) {
        return false;
    }
    png_clear(table, table_count * sizeof(*table));

    uint16_t used = 0u;
    for (uint16_t symbol = 0u; symbol < symbol_count; ++symbol) {
        uint8_t length = lengths[symbol];
        if (length > max_bits) return false;
        if (length != 0u) {
            ++counts[length];
            ++used;
        }
    }
    if (used == 0u) return false;

    uint32_t code = 0u;
    for (uint8_t bits = 1u; bits <= max_bits; ++bits) {
        code = (code + counts[bits - 1u]) << 1;
        if (code > (UINT32_C(1) << bits)) return false;
        next_code[bits] = (uint16_t)code;
    }
    if (code + counts[max_bits] > (UINT32_C(1) << max_bits)) return false;

    for (uint16_t symbol = 0u; symbol < symbol_count; ++symbol) {
        uint8_t length = lengths[symbol];
        if (length == 0u) continue;
        uint16_t canonical = next_code[length]++;
        uint16_t reversed = png_reverse_bits(canonical, length);
        size_t suffix_count = (size_t)1u << (max_bits - length);
        for (size_t suffix = 0u; suffix < suffix_count; ++suffix) {
            size_t index = (size_t)reversed | (suffix << length);
            if (index >= table_count || table[index].valid != 0u) return false;
            table[index].symbol = symbol;
            table[index].bits = length;
            table[index].valid = 1u;
        }
    }
    return true;
}

static bool png_huffman_decode(
    struct png_bit_reader *reader,
    const struct png_huffman_entry *table,
    uint8_t table_bits,
    uint16_t *out_symbol
) {
    if (reader == NULL || table == NULL || out_symbol == NULL ||
        table_bits == 0u || table_bits > 15u) {
        return false;
    }

    while (reader->bit_count < table_bits && reader->offset < reader->size) {
        reader->bit_buffer |=
            (uint64_t)reader->data[reader->offset++] << reader->bit_count;
        reader->bit_count += 8u;
    }

    uint32_t mask = (UINT32_C(1) << table_bits) - 1u;
    const struct png_huffman_entry *entry =
        &table[(uint32_t)reader->bit_buffer & mask];
    if (entry->valid == 0u || entry->bits > reader->bit_count) return false;

    *out_symbol = entry->symbol;
    reader->bit_buffer >>= entry->bits;
    reader->bit_count -= entry->bits;
    return true;
}

static bool png_build_fixed_tables(
    struct png_huffman_entry *literal,
    struct png_huffman_entry *distance
) {
    uint8_t literal_lengths[288];
    uint8_t distance_lengths[32];
    for (uint16_t i = 0u; i <= 143u; ++i) literal_lengths[i] = 8u;
    for (uint16_t i = 144u; i <= 255u; ++i) literal_lengths[i] = 9u;
    for (uint16_t i = 256u; i <= 279u; ++i) literal_lengths[i] = 7u;
    for (uint16_t i = 280u; i <= 287u; ++i) literal_lengths[i] = 8u;
    for (uint16_t i = 0u; i < 32u; ++i) distance_lengths[i] = 5u;

    return png_build_huffman(
            literal_lengths, 288u, PNG_HUFFMAN_BITS,
            literal, PNG_HUFFMAN_TABLE_SIZE) &&
        png_build_huffman(
            distance_lengths, 32u, PNG_HUFFMAN_BITS,
            distance, PNG_HUFFMAN_TABLE_SIZE);
}

static bool png_build_dynamic_tables(
    struct png_bit_reader *reader,
    struct png_huffman_entry *literal,
    struct png_huffman_entry *distance,
    struct png_huffman_entry *code_length_table
) {
    static const uint8_t order[19] = {
        16u, 17u, 18u, 0u, 8u, 7u, 9u, 6u, 10u, 5u,
        11u, 4u, 12u, 3u, 13u, 2u, 14u, 1u, 15u
    };
    uint32_t value;
    if (!png_bits_read(reader, 5u, &value)) return false;
    uint16_t hlit = (uint16_t)(value + 257u);
    if (!png_bits_read(reader, 5u, &value)) return false;
    uint16_t hdist = (uint16_t)(value + 1u);
    if (!png_bits_read(reader, 4u, &value)) return false;
    uint16_t hclen = (uint16_t)(value + 4u);
    if (hlit > 286u || hdist > 32u || hclen > 19u) return false;

    uint8_t code_lengths[19];
    uint8_t combined[318];
    png_clear(code_lengths, sizeof(code_lengths));
    png_clear(combined, sizeof(combined));

    for (uint16_t i = 0u; i < hclen; ++i) {
        if (!png_bits_read(reader, 3u, &value)) return false;
        code_lengths[order[i]] = (uint8_t)value;
    }
    if (!png_build_huffman(
            code_lengths, 19u, PNG_CODELEN_BITS,
            code_length_table, PNG_CODELEN_TABLE_SIZE)) {
        return false;
    }

    uint16_t total = (uint16_t)(hlit + hdist);
    uint16_t index = 0u;
    while (index < total) {
        uint16_t symbol;
        if (!png_huffman_decode(
                reader, code_length_table, PNG_CODELEN_BITS, &symbol)) {
            return false;
        }
        if (symbol <= 15u) {
            combined[index++] = (uint8_t)symbol;
            continue;
        }

        uint32_t repeat;
        uint8_t repeated_value = 0u;
        if (symbol == 16u) {
            if (index == 0u || !png_bits_read(reader, 2u, &repeat)) return false;
            repeat += 3u;
            repeated_value = combined[index - 1u];
        } else if (symbol == 17u) {
            if (!png_bits_read(reader, 3u, &repeat)) return false;
            repeat += 3u;
        } else if (symbol == 18u) {
            if (!png_bits_read(reader, 7u, &repeat)) return false;
            repeat += 11u;
        } else {
            return false;
        }

        if ((uint32_t)index + repeat > total) return false;
        for (uint32_t i = 0u; i < repeat; ++i) {
            combined[index++] = repeated_value;
        }
    }

    if (combined[256] == 0u) return false;
    return png_build_huffman(
            combined, hlit, PNG_HUFFMAN_BITS,
            literal, PNG_HUFFMAN_TABLE_SIZE) &&
        png_build_huffman(
            combined + hlit, hdist, PNG_HUFFMAN_BITS,
            distance, PNG_HUFFMAN_TABLE_SIZE);
}

static bool png_inflate_codes(
    struct png_bit_reader *reader,
    const struct png_huffman_entry *literal,
    const struct png_huffman_entry *distance,
    uint8_t *output,
    size_t capacity,
    size_t *written
) {
    static const uint16_t length_base[29] = {
        3u,4u,5u,6u,7u,8u,9u,10u,11u,13u,15u,17u,19u,23u,27u,
        31u,35u,43u,51u,59u,67u,83u,99u,115u,131u,163u,195u,227u,258u
    };
    static const uint8_t length_extra[29] = {
        0u,0u,0u,0u,0u,0u,0u,0u,1u,1u,1u,1u,2u,2u,2u,2u,
        3u,3u,3u,3u,4u,4u,4u,4u,5u,5u,5u,5u,0u
    };
    static const uint16_t distance_base[30] = {
        1u,2u,3u,4u,5u,7u,9u,13u,17u,25u,33u,49u,65u,97u,129u,
        193u,257u,385u,513u,769u,1025u,1537u,2049u,3073u,4097u,
        6145u,8193u,12289u,16385u,24577u
    };
    static const uint8_t distance_extra[30] = {
        0u,0u,0u,0u,1u,1u,2u,2u,3u,3u,4u,4u,5u,5u,6u,
        6u,7u,7u,8u,8u,9u,9u,10u,10u,11u,11u,12u,12u,13u,13u
    };

    for (;;) {
        uint16_t symbol;
        if (!png_huffman_decode(
                reader, literal, PNG_HUFFMAN_BITS, &symbol)) {
            return false;
        }
        if (symbol < 256u) {
            if (*written >= capacity) return false;
            output[(*written)++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256u) return true;
        if (symbol < 257u || symbol > 285u) return false;

        uint16_t length_index = (uint16_t)(symbol - 257u);
        uint32_t extra = 0u;
        if (!png_bits_read(reader, length_extra[length_index], &extra)) {
            return false;
        }
        size_t length = (size_t)length_base[length_index] + extra;

        uint16_t distance_symbol;
        if (!png_huffman_decode(
                reader, distance, PNG_HUFFMAN_BITS, &distance_symbol) ||
            distance_symbol >= 30u) {
            return false;
        }
        extra = 0u;
        if (!png_bits_read(reader, distance_extra[distance_symbol], &extra)) {
            return false;
        }
        size_t back = (size_t)distance_base[distance_symbol] + extra;
        if (back == 0u || back > *written || length > capacity - *written) {
            return false;
        }
        for (size_t i = 0u; i < length; ++i) {
            output[*written] = output[*written - back];
            ++(*written);
        }
    }
}

static bool png_inflate_zlib(
    const uint8_t *data,
    size_t size,
    uint8_t *output,
    size_t capacity
) {
    if (data == NULL || output == NULL || size < 6u) return false;
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    if ((cmf & 0x0Fu) != 8u || (cmf >> 4) > 7u ||
        ((((uint16_t)cmf << 8) | flg) % 31u) != 0u ||
        (flg & 0x20u) != 0u) {
        return false;
    }

    struct png_huffman_entry *literal = kheap_alloc(
        PNG_HUFFMAN_TABLE_SIZE * sizeof(*literal), 16u);
    struct png_huffman_entry *distance = kheap_alloc(
        PNG_HUFFMAN_TABLE_SIZE * sizeof(*distance), 16u);
    struct png_huffman_entry *code_length = kheap_alloc(
        PNG_CODELEN_TABLE_SIZE * sizeof(*code_length), 16u);
    if (literal == NULL || distance == NULL || code_length == NULL) {
        if (literal != NULL) (void)kheap_free_sized(
            literal, PNG_HUFFMAN_TABLE_SIZE * sizeof(*literal));
        if (distance != NULL) (void)kheap_free_sized(
            distance, PNG_HUFFMAN_TABLE_SIZE * sizeof(*distance));
        if (code_length != NULL) (void)kheap_free_sized(
            code_length, PNG_CODELEN_TABLE_SIZE * sizeof(*code_length));
        return false;
    }

    struct png_bit_reader reader;
    reader.data = data + 2u;
    reader.size = size - 6u;
    reader.offset = 0u;
    reader.bit_buffer = 0u;
    reader.bit_count = 0u;

    size_t written = 0u;
    bool final_block = false;
    bool ok = true;
    while (!final_block && ok) {
        uint32_t final_value;
        uint32_t block_type;
        if (!png_bits_read(&reader, 1u, &final_value) ||
            !png_bits_read(&reader, 2u, &block_type)) {
            ok = false;
            break;
        }
        final_block = final_value != 0u;

        if (block_type == 0u) {
            png_bits_align_byte(&reader);
            uint32_t len;
            uint32_t nlen;
            if (!png_bits_read(&reader, 16u, &len) ||
                !png_bits_read(&reader, 16u, &nlen) ||
                ((len ^ UINT32_C(0xFFFF)) & UINT32_C(0xFFFF)) != nlen ||
                len > capacity - written) {
                ok = false;
                break;
            }
            for (uint32_t i = 0u; i < len; ++i) {
                uint32_t byte;
                if (!png_bits_read(&reader, 8u, &byte)) {
                    ok = false;
                    break;
                }
                output[written++] = (uint8_t)byte;
            }
        } else if (block_type == 1u || block_type == 2u) {
            bool tables_ok = block_type == 1u
                ? png_build_fixed_tables(literal, distance)
                : png_build_dynamic_tables(
                    &reader, literal, distance, code_length);
            if (!tables_ok || !png_inflate_codes(
                    &reader, literal, distance,
                    output, capacity, &written)) {
                ok = false;
            }
        } else {
            ok = false;
        }
    }

    uint32_t expected_adler = png_read_be32(data + size - 4u);
    if (!final_block || written != capacity ||
        png_adler32(output, written) != expected_adler) {
        ok = false;
    }

    (void)kheap_free_sized(
        code_length, PNG_CODELEN_TABLE_SIZE * sizeof(*code_length));
    (void)kheap_free_sized(
        distance, PNG_HUFFMAN_TABLE_SIZE * sizeof(*distance));
    (void)kheap_free_sized(
        literal, PNG_HUFFMAN_TABLE_SIZE * sizeof(*literal));
    return ok;
}

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c) {
    int32_t p = (int32_t)a + (int32_t)b - (int32_t)c;
    int32_t pa = p - (int32_t)a;
    int32_t pb = p - (int32_t)b;
    int32_t pc = p - (int32_t)c;
    if (pa < 0) pa = -pa;
    if (pb < 0) pb = -pb;
    if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static bool png_unfilter(
    const uint8_t *filtered,
    size_t filtered_size,
    uint32_t width,
    uint32_t height,
    uint8_t channels,
    uint8_t *pixels,
    size_t pixel_size
) {
    if (filtered == NULL || pixels == NULL || channels == 0u) return false;
    size_t stride = (size_t)width * channels;
    if (width != 0u && stride / width != channels) return false;
    if ((stride + 1u) > SIZE_MAX / height ||
        filtered_size != (stride + 1u) * height ||
        stride > SIZE_MAX / height || pixel_size != stride * height) {
        return false;
    }

    for (uint32_t y = 0u; y < height; ++y) {
        const uint8_t *source = filtered + (size_t)y * (stride + 1u);
        uint8_t filter = source[0];
        source += 1u;
        uint8_t *row = pixels + (size_t)y * stride;
        const uint8_t *previous = y == 0u ? NULL : row - stride;
        if (filter > 4u) return false;

        for (size_t x = 0u; x < stride; ++x) {
            uint8_t left = x < channels ? 0u : row[x - channels];
            uint8_t up = previous == NULL ? 0u : previous[x];
            uint8_t up_left =
                previous == NULL || x < channels ? 0u : previous[x - channels];
            uint8_t predictor = 0u;
            if (filter == 1u) predictor = left;
            else if (filter == 2u) predictor = up;
            else if (filter == 3u) predictor =
                (uint8_t)(((uint16_t)left + (uint16_t)up) / 2u);
            else if (filter == 4u) predictor = png_paeth(left, up, up_left);
            row[x] = (uint8_t)(source[x] + predictor);
        }
    }
    return true;
}

void aurora_png_release(struct aurora_png_image *image) {
    if (image == NULL) return;
    if (image->pixels != NULL && image->pixel_bytes != 0u) {
        (void)kheap_free_sized(image->pixels, image->pixel_bytes);
    }
    png_clear(image, sizeof(*image));
}

bool aurora_png_decode(
    const uint8_t *data,
    size_t size,
    struct aurora_png_image *out
) {
    static const uint8_t signature[PNG_SIGNATURE_SIZE] = {
        137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
    };
    if (data == NULL || out == NULL || size < PNG_SIGNATURE_SIZE + 12u) {
        return false;
    }
    png_clear(out, sizeof(*out));
    for (size_t i = 0u; i < PNG_SIGNATURE_SIZE; ++i) {
        if (data[i] != signature[i]) return false;
    }

    size_t offset = PNG_SIGNATURE_SIZE;
    bool saw_ihdr = false;
    bool saw_iend = false;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint8_t channels = 0u;
    size_t compressed_size = 0u;

    while (offset + 12u <= size) {
        uint32_t length = png_read_be32(data + offset);
        if ((size_t)length > size - offset - 12u) return false;
        const uint8_t *type = data + offset + 4u;
        const uint8_t *payload = data + offset + 8u;
        uint32_t crc = png_read_be32(payload + length);
        if (!png_chunk_crc_ok(type, payload, length, crc)) return false;

        bool is_ihdr = type[0]=='I' && type[1]=='H' && type[2]=='D' && type[3]=='R';
        bool is_idat = type[0]=='I' && type[1]=='D' && type[2]=='A' && type[3]=='T';
        bool is_iend = type[0]=='I' && type[1]=='E' && type[2]=='N' && type[3]=='D';
        if (is_ihdr) {
            if (saw_ihdr || length != 13u || offset != PNG_SIGNATURE_SIZE) return false;
            width = png_read_be32(payload);
            height = png_read_be32(payload + 4u);
            uint8_t bit_depth = payload[8];
            uint8_t color_type = payload[9];
            uint8_t compression = payload[10];
            uint8_t filter = payload[11];
            uint8_t interlace = payload[12];
            if (width == 0u || height == 0u || bit_depth != 8u ||
                (color_type != 2u && color_type != 6u) ||
                compression != 0u || filter != 0u || interlace != 0u) {
                return false;
            }
            channels = color_type == 2u ? 3u : 4u;
            saw_ihdr = true;
        } else if (is_idat) {
            if (!saw_ihdr || saw_iend ||
                compressed_size > SIZE_MAX - (size_t)length) return false;
            compressed_size += length;
        } else if (is_iend) {
            if (!saw_ihdr || length != 0u) return false;
            saw_iend = true;
            offset += 12u;
            break;
        }
        offset += 12u + (size_t)length;
    }

    if (!saw_ihdr || !saw_iend || compressed_size < 6u) return false;
    if ((size_t)width > SIZE_MAX / channels) return false;
    size_t stride = (size_t)width * channels;
    if (stride == SIZE_MAX || stride + 1u > SIZE_MAX / height ||
        stride > SIZE_MAX / height) {
        return false;
    }
    size_t filtered_size = (stride + 1u) * height;
    size_t pixel_size = stride * height;

    uint8_t *compressed = (uint8_t *)kheap_alloc(compressed_size, 16u);
    uint8_t *filtered = (uint8_t *)kheap_alloc(filtered_size, 16u);
    uint8_t *pixels = (uint8_t *)kheap_alloc(pixel_size, 16u);
    if (compressed == NULL || filtered == NULL || pixels == NULL) {
        if (pixels != NULL) (void)kheap_free_sized(pixels, pixel_size);
        if (filtered != NULL) (void)kheap_free_sized(filtered, filtered_size);
        if (compressed != NULL) (void)kheap_free_sized(compressed, compressed_size);
        return false;
    }

    offset = PNG_SIGNATURE_SIZE;
    size_t compressed_offset = 0u;
    while (offset + 12u <= size) {
        uint32_t length = png_read_be32(data + offset);
        const uint8_t *type = data + offset + 4u;
        const uint8_t *payload = data + offset + 8u;
        bool is_idat = type[0]=='I' && type[1]=='D' && type[2]=='A' && type[3]=='T';
        bool is_iend = type[0]=='I' && type[1]=='E' && type[2]=='N' && type[3]=='D';
        if (is_idat) {
            png_copy(compressed + compressed_offset, payload, length);
            compressed_offset += length;
        }
        offset += 12u + (size_t)length;
        if (is_iend) break;
    }

    bool ok = compressed_offset == compressed_size &&
        png_inflate_zlib(compressed, compressed_size, filtered, filtered_size) &&
        png_unfilter(
            filtered, filtered_size, width, height, channels,
            pixels, pixel_size);

    (void)kheap_free_sized(filtered, filtered_size);
    (void)kheap_free_sized(compressed, compressed_size);
    if (!ok) {
        (void)kheap_free_sized(pixels, pixel_size);
        return false;
    }

    out->width = width;
    out->height = height;
    out->channels = channels;
    out->pixels = pixels;
    out->pixel_bytes = pixel_size;
    return true;
}
