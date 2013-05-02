/*! A binary calculator for unsigned, signed, and floating point encodings.
    Features:
        8,16,32,64 bit signed/unsigned and 32,64 bit floating point modes
        Verbose mode showing computation steps
        Hexadecimal input and output
        Unary operators: ~ -
        Binary operators: * / % + - << >> & ^ |

    Keith Campbell (kacampb2@illinois.edu)
*/

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#include <stdexcept>

#include <readline/readline.h>
#include <readline/history.h>

enum encoding_t
{
    S8, S16, S32, S64,
    U8, U16, U32, U64,
    F32, F64,
    NUM_ENCODINGS,
    INVALID_ENCODING = -1,
};

struct encoded_value
{
    encoding_t encoding;
    union
    {
        int8_t s8;
        int16_t s16;
        int32_t s32;
        int64_t s64;

        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;

        float f32;
        double f64;
    };
};

static const char * encoding_names[NUM_ENCODINGS] =
{ "s8", "s16", "s32", "s64",
  "u8", "u16", "u32", "u64",
  "f32", "f64" };

static const encoded_value INVALID_VALUE = {INVALID_ENCODING};

enum operator_t
{
    OPEN_PAREN,
    NOT,
    NEGATE,
    MULTIPLY,
    DIVIDE,
    MODULUS,
    ADD,
    SUBTRACT,
    LEFT_SHIFT,
    RIGHT_SHIFT,
    AND,
    XOR,
    OR,
    CLOSE_PAREN,
    END_EXPRESSION,
    NUM_OPS,
    INVALID_OP = -1,
};

enum arity_t
{
    UNARY,
    BINARY,
    SENTINEL,
};

static struct
{
    int precedence;
    arity_t arity;
    const char * identifier;
} operator_table[] =
{ { 8, UNARY, "(" },
  { 7, UNARY, "~" },
  { 7, UNARY, "-" },
  { 6, BINARY, "*" },
  { 6, BINARY, "/" },
  { 6, BINARY, "%" },
  { 5, BINARY, "+" },
  { 5, BINARY, "-" },
  { 4, BINARY, "<<" },
  { 4, BINARY, ">>" },
  { 3, BINARY, "&" },
  { 2, BINARY, "^" },
  { 1, BINARY, "|" },
  { 0, SENTINEL, ")" },
  { 0, SENTINEL, "\0" } };

struct parse_error : std::runtime_error
{
    parse_error() : std::runtime_error("Parse error") {}
};

struct range_error : std::runtime_error
{
    range_error() : std::runtime_error("Value out of range") {}
};

static bool verbose;

static int max(int a, int b)
{
    return a > b ? a : b;
}

static void skip_whitespace(char *& cursor)
{
    while (isspace(*cursor))
    {
        cursor++;
    }
}

static operator_t parse_operator(char *& cursor, arity_t arity,
                                 operator_t sentinel = INVALID_OP)
{
    skip_whitespace(cursor);
    for (int op = 0; op < NUM_OPS; op++)
    {
        if (operator_table[op].arity != arity && op != sentinel)
        {
            continue;
        }
        const char * identifier = operator_table[op].identifier;
        int identifier_size = max(1, strlen(identifier));
        if (strncmp(cursor, identifier, identifier_size) == 0)
        {
            cursor += identifier_size;
            return (operator_t)op;
        }
    }
    throw parse_error();
}

static int64_t parse_int(char *& cursor, int64_t min, int64_t max)
{
    int64_t value = strtoll(cursor, &cursor, 10);
    if (value < min || value > max)
    {
        errno = ERANGE;
    }
    return value;
}

static uint64_t parse_uint(char *& cursor, uint64_t max)
{
    if (cursor[0] == '-' && isdigit(cursor[1]))
    {
        errno = ERANGE;
        return 0;
    }
    uint64_t value = strtoull(cursor, &cursor, 10);
    if (value > max)
    {
        errno = ERANGE;
    }
    return value;
}

static int digit_to_int(int c)
{
    return '0' <= c && c <= '9' ? c - '0' :
           'a' <= c && c <= 'f' ? c - 'a' + 10 :
           'A' <= c && c <= 'F' ? c - 'A' + 10 : 0;
}

static uint64_t strtox(char *& cursor, int max_digits)
/*! Unfortunately, strtol forces you to use "0x" as a prefix, so
    rather than hack around it; I'm going to reimplement it with
    an "x" prefix */
{
    if (cursor[0] != 'x' || !isxdigit(cursor[1]))
    {
        return 0;
    }
    cursor++;
    while (*cursor == '0')
    {
        cursor++;
    }
    if (!isxdigit(*cursor))
    {
        return 0;
    }
    uint64_t value = 0;
    for (int digits = 0; digits < max_digits; digits++)
    {
        value += digit_to_int(*cursor);
        cursor++;
        if (!isxdigit(*cursor))
        {
            return value;
        }
        value <<= 4;
    }
    errno = ERANGE;
    return 0;
}

static encoded_value parse_value(char *& cursor, encoding_t mode)
{
    skip_whitespace(cursor);
    encoded_value value;
    value.encoding = mode;

    char * old_cursor = cursor;
    if (*cursor == 'x')
    {
        switch (mode)
        {
        case S8:
        case U8:
            value.u8 = strtox(cursor, 2);
            break;
        case S16:
        case U16:
            value.u16 = strtox(cursor, 4);
            break;
        case S32:
        case U32:
        case F32:
            value.u32 = strtox(cursor, 8);
            break;
        case S64:
        case U64:
        case F64:
            value.u64 = strtox(cursor, 16);
            break;
        default:
            fprintf(stderr, "parse_value: Invalid mode: %d\n", (int)mode);
            break;
        }
    }
    else
    {
        switch (mode)
        {
        case S8:
            value.s8 = parse_int(cursor, INT8_MIN, INT8_MAX);
            break;
        case S16:
            value.s16 = parse_int(cursor, INT16_MIN, INT16_MAX);
            break;
        case S32:
            value.s32 = parse_int(cursor, INT32_MIN, INT32_MAX);
            break;
        case S64:
            value.s64 = parse_int(cursor, INT64_MIN, INT64_MAX);
            break;
        case U8:
            value.u8 = parse_uint(cursor, UINT8_MAX);
            break;
        case U16:
            value.u16 = parse_uint(cursor, UINT16_MAX);
            break;
        case U32:
            value.u32 = parse_uint(cursor, UINT32_MAX);
            break;
        case U64:
            value.u64 = parse_uint(cursor, UINT64_MAX);
            break;
        case F32:
            value.f32 = strtof(cursor, &cursor);
            break;
        case F64:
            value.f64 = strtod(cursor, &cursor);
            break;
        default:
            fprintf(stderr, "parse_value: Invalid mode: %d\n", (int)mode);
            break;
        }
    }

    if (errno == ERANGE)
    {
        errno = 0;
        cursor = old_cursor;
        throw range_error();
    }
    if (old_cursor == cursor)
    {
        return INVALID_VALUE;
    }
    else
    {
        return value;
    }
}

static char * format_hex(encoded_value value)
{
    static char string_buf[256];
    static int index = 0;
    index = (index + 32) % 256;
    char * string = &string_buf[index];

    switch (value.encoding)
    {
    case S8:
    case U8:
        sprintf(string, "x%02" PRIx8, value.u8);
        break;
    case S16:
    case U16:
        sprintf(string, "x%04" PRIx16, value.u16);
        break;
    case S32:
    case U32:
    case F32:
        sprintf(string, "x%08" PRIx32, value.u32);
        break;
    case S64:
    case U64:
    case F64:
        sprintf(string, "x%016" PRIx64, value.u64);
        break;
    default:
        fprintf(stderr, "Invalid encoding: %d\n", value.encoding);
        break;
    }
    return string;
}

static char * format_dec(encoded_value value)
{
    static char string_buf[256];
    static int index = 0;
    index = (index + 32) % 256;
    char * string = &string_buf[index];

    switch (value.encoding)
    {
    case S8:
        sprintf(string, "%" PRId8, value.s8);
        break;
    case S16:
        sprintf(string, "%" PRId16, value.s16);
        break;
    case S32:
        sprintf(string, "%" PRId32, value.s32);
        break;
    case S64:
        sprintf(string, "%" PRId64, value.s64);
        break;
    case U8:
        sprintf(string, "%" PRIu8, value.u8);
        break;
    case U16:
        sprintf(string, "%" PRIu16, value.u16);
        break;
    case U32:
        sprintf(string, "%" PRIu32, value.u32);
        break;
    case U64:
        sprintf(string, "%" PRIu64, value.u64);
        break;
    case F32:
        sprintf(string, "%f", value.f32);
        break;
    case F64:
        sprintf(string, "%lf", value.f64);
        break;
    default:
        fprintf(stderr, "Invalid encoding: %d\n", value.encoding);
        break;
    }
    return string;
}

template <typename type>
static bool evaluate_operator_integer(operator_t op, type value, type & result)
{
    bool success = true;
    switch (op)
    {
    case NOT:
        result = ~value;
        break;
    case NEGATE:
        result = -value;
        break;
    default:
        success = false;
        break;
    }
    return success;
}

template <typename type>
static bool evaluate_operator_real(operator_t op, type value, type & result)
{
    bool success = true;
    switch (op)
    {
    case NEGATE:
        result = -value;
        break;
    default:
        success = false;
        break;
    }
    return success;
}

static encoded_value evaluate_operator(operator_t op, encoded_value value)
{
    bool success = false;
    encoded_value result = {};
    result.encoding = value.encoding;
    switch (result.encoding)
    {
    case S8:
        success = evaluate_operator_integer<int8_t>(op, value.s8, result.s8);
        break;
    case S16:
        success = evaluate_operator_integer<int16_t>(op, value.s16, result.s16);
        break;
    case S32:
        success = evaluate_operator_integer<int32_t>(op, value.s32, result.s32);
        break;
    case S64:
        success = evaluate_operator_integer<int64_t>(op, value.s64, result.s64);
        break;
    case U8:
        success = evaluate_operator_integer<uint8_t>(op, value.u8, result.u8);
        break;
    case U16:
        success = evaluate_operator_integer<uint16_t>(op, value.u16, result.u16);
        break;
    case U32:
        success = evaluate_operator_integer<uint32_t>(op, value.u32, result.u32);
        break;
    case U64:
        success = evaluate_operator_integer<uint64_t>(op, value.u64, result.u64);
        break;
    case F32:
        success = evaluate_operator_real<float>(op, value.f32, result.f32);
        break;
    case F64:
        success = evaluate_operator_real<double>(op, value.f64, result.f64);
        break;
    default:
        fprintf(stderr, "evaluate_operator: invalid encoding: %d\n", (int)result.encoding);
        break;
    }

    if (success)
    {
        if (verbose)
        {
            printf("%s(%s) = %s (%s%s = %s)\n",
                   operator_table[op].identifier, format_dec(value), format_dec(result),
                   operator_table[op].identifier, format_hex(value), format_hex(result));
        }
        return result;
    }
    else
    {
        throw parse_error();
    }
}

template <typename type>
static bool evaluate_operator_integer(operator_t op, type left, type right, type & result)
{
    bool success = true;
    switch (op)
    {
    case ADD:
        result = left + right;
        break;
    case SUBTRACT:
        result = left - right;
        break;
    case MULTIPLY:
        result = left * right;
        break;
    case DIVIDE:
        result = left / right;
        break;
    case MODULUS:
        result = left % right;
        break;
    case LEFT_SHIFT:
        result = left << right;
        break;
    case RIGHT_SHIFT:
        result = left >> right;
        break;
    case AND:
        result = left & right;
        break;
    case OR:
        result = left | right;
        break;
    case XOR:
        result = left ^ right;
        break;
    default:
        success = false;
        break;
    }
    return success;
}

template <typename type>
static bool evaluate_operator_real(operator_t op, type left, type right, type & result)
{
    bool success = true;
    switch (op)
    {
    case ADD:
        result = left + right;
        break;
    case SUBTRACT:
        result = left - right;
        break;
    case MULTIPLY:
        result = left * right;
        break;
    case DIVIDE:
        result = left / right;
        break;
    default:
        success = false;
        break;
    }
    return success;
}

static encoded_value evaluate_operator(operator_t op, encoded_value left, encoded_value right)
{
    encoded_value result = {};
    if (left.encoding != right.encoding)
    {
        fprintf(stderr, "Mixed types not supported\n");
        return INVALID_VALUE;
    }

    bool success = false;
    result.encoding = left.encoding;
    switch (result.encoding)
    {
    case S8:
        success = evaluate_operator_integer<int8_t>(op, left.s8, right.s8, result.s8);
        break;
    case S16:
        success = evaluate_operator_integer<int16_t>(op, left.s16, right.s16, result.s16);
        break;
    case S32:
        success = evaluate_operator_integer<int32_t>(op, left.s32, right.s32, result.s32);
        break;
    case S64:
        success = evaluate_operator_integer<int64_t>(op, left.s64, right.s64, result.s64);
        break;
    case U8:
        success = evaluate_operator_integer<uint8_t>(op, left.u8, right.u8, result.u8);
        break;
    case U16:
        success = evaluate_operator_integer<uint16_t>(op, left.u16, right.u16, result.u16);
        break;
    case U32:
        success = evaluate_operator_integer<uint32_t>(op, left.u32, right.u32, result.u32);
        break;
    case U64:
        success = evaluate_operator_integer<uint64_t>(op, left.u64, right.u64, result.u64);
        break;
    case F32:
        success = evaluate_operator_real<float>(op, left.f32, right.f32, result.f32);
        break;
    case F64:
        success = evaluate_operator_real<double>(op, left.f64, right.f64, result.f64);
        break;
    default:
        fprintf(stderr, "evaluate_operator: invalid encoding: %d\n", (int)result.encoding);
        break;
    }
    if (success)
    {
        if (verbose)
        {
            printf("%s %s %s = %s (%s %s %s = %s)\n",
                   format_dec(left), operator_table[op].identifier, format_dec(right), format_dec(result),
                   format_hex(left), operator_table[op].identifier, format_hex(right), format_hex(result));
        }
        return result;
    }
    else
    {
        throw parse_error();
    }
}

static encoded_value compute_value(char *& cursor, encoding_t mode);

static encoded_value compute_expression(char *& cursor, encoding_t mode, operator_t sentinel,
                                        int min_precedence = 0, operator_t * op_out = NULL)
{
    encoded_value value = compute_value(cursor, mode);
    operator_t op = parse_operator(cursor, BINARY, sentinel);
    while (true)
    {
        int precedence = operator_table[op].precedence;
        if (precedence <= min_precedence)
        {
            break;
        }
        operator_t next_op;
        encoded_value next_value = compute_expression(cursor, mode, sentinel, precedence, &next_op);
        value = evaluate_operator(op, value, next_value);
        op = next_op;
    }
    if (op_out)
    {
        *op_out = op;
    }
    return value;
}

static encoded_value compute_value(char *& cursor, encoding_t mode)
{
    encoded_value value = parse_value(cursor, mode);
    if (value.encoding != INVALID_ENCODING)
    {
        return value;
    }
    operator_t unary_op = parse_operator(cursor, UNARY);
    if (unary_op == OPEN_PAREN)
    {
        return compute_expression(cursor, mode, CLOSE_PAREN);
    }
    else if (operator_table[unary_op].arity == UNARY)
    {
        encoded_value result = compute_value(cursor, mode);
        return evaluate_operator(unary_op, result);
    }
    else
    {
        throw parse_error();
    }
}

static encoding_t parse_mode(char * mode_name)
{
    for (int mode = 0; mode < NUM_ENCODINGS; mode++)
    {
        if (strcmp(mode_name, encoding_names[mode]) == 0)
        {
            return (encoding_t)mode;
        }
    }
    return INVALID_ENCODING;
}

static bool handle_input(char * input, encoding_t mode)
{
    char * cursor = input;
    try
    {
        encoded_value result = compute_expression(cursor, mode, END_EXPRESSION);
        printf("%s (%s)\n", format_dec(result), format_hex(result));
        return true;
    }
    catch (std::exception & error)
    {
        fprintf(stderr, "  ");
        for (; cursor > input; cursor--)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "^\n");
        fprintf(stderr, "%s\n", error.what());
        return false;
    }
}

void usage(char * me)
{
    fprintf(stderr, "Usage: %s [-v] mode\n"
                    "-v: Be verbose, print each computation step\n"
                    "mode: one of the following:\n"
                    "  s8,s16,s32,s64: Use 8,16,32,64 bit signed encoding\n"
                    "  u8,u16,u32,u64: Use 8,16,32,64 bit unsigned encoding\n"
                    "  f32,f64: Use 32 or 64 bit floating-point encoding\n", me);
}

int main(int argc, char * argv[])
{
    verbose = false;
    if (argv[1] && strcmp(argv[1], "-v") == 0)
    {
        verbose = true;
        argv++;
        argc--;
    }

    if (argc != 2)
    {
        usage(argv[0]);
        return 1;
    }
    encoding_t mode = parse_mode(argv[1]);
    if (mode == INVALID_ENCODING)
    {
        usage(argv[0]);
        return 1;
    }

    while (true)
    {
        char * input = readline("> ");
        if (!input)
        {
            break;
        }
        if (*input == '\00')
        {
            continue;
        }
        if (strcmp(input, "exit") == 0)
        {
            break;
        }
        add_history(input);
        bool success = handle_input(input, mode);
        if (success)
        {
            add_history(input);
        }
    }
    return 0;
}
