#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "ft_stdio.h"
#include "ft_string.h"
#include "heuristic.h"
#include "npuzzle.h"
#include "zobrist.h"

static void npuzzle_init_tag_array(uint16_t *tagArray, size_t size)
{
    size_t counter = 0;

    // Initialize the tag array values in a spiral shape.
    for (size_t layer = 0; layer < size / 2; ++layer)
    {
        size_t boxMin = layer;
        size_t boxMax = size - layer - 1;

        for (size_t x = boxMin; x <= boxMax; ++x)
            tagArray[boxMin * size + x] = ++counter;

        for (size_t y = boxMin + 1; y <= boxMax; ++y)
            tagArray[y * size + boxMax] = ++counter;

        for (size_t x = boxMax - 1; x + 1 > boxMin; --x)
            tagArray[boxMax * size + x] = ++counter;

        for (size_t y = boxMax - 1; y > boxMin; --y)
            tagArray[y * size + boxMin] = ++counter;
    }

    // Add the hole (represented by a 0) to the tag array.
    tagArray[(size / 2) * size + (size - 1) / 2] = 0;
}

static int npuzzle_parse_size(NPuzzle *np, char *line, size_t readSize)
{
    if (ft_strlen(line) != readSize)
    {
        ft_dputstr("Parsing error: nullbytes in string\n", STDERR_FILENO);
        return -1;
    }

    line[readSize - 1] = '\0';

    char *ptr = line + ft_strspn(line, " \t");

    // Empty line or start of comment, skip line.
    if (*ptr == '#' || *ptr == '\0')
        return 0;

    np->size = strtoul(ptr, &ptr, 10);
    ptr += ft_strspn(line, " \t");

    // If there's more info on the line, that's an error.
    if (*ptr != '#' && *ptr != '\0')
    {
        ft_dprintf(STDERR_FILENO, "Parsing error: invalid data '%s' after puzzle size\n", ptr);
        return -1;
    }

    return 1;
}

static int npuzzle_parse_row(NPuzzle *np, char *line, size_t readSize, size_t yLen)
{
    if (ft_strlen(line) != readSize)
    {
        ft_dputstr("Parsing error: nullbytes in string\n", STDERR_FILENO);
        return -1;
    }

    line[readSize - 1] = '\0';

    char *ptr = line + ft_strspn(line, " \t");

    // Empty line or start of comment, skip line.
    if (*ptr == '#' || *ptr == '\0')
        return 0;

    // Line isn't empty, yet we finish parsing the board, return an error.
    if (yLen == np->size)
    {
        ft_dprintf(STDERR_FILENO, "Parsing error: '%s' found even though the puzzle is complete\n", ptr);
        return -1;
    }

    for (size_t xLen = 0; xLen < np->size; ++xLen)
    {
        char *initialPtr = ptr;
        uint16_t value = strtoul(ptr, &ptr, 10);

        // Check if strtoul() failed to parse any number at all.
        if (initialPtr == ptr)
        {
            ft_dprintf(STDERR_FILENO, "Parsing error: garbage in line or missing pieces\n");
            ft_dprintf(STDERR_FILENO,
                "(Note: expected a number, got '%.*s')\n",
                (int)ft_strcspn(ptr, " \t"), ptr);
            return -1;
        }

        // Check if the piece index fits in the board.
        if (value >= np->size * np->size)
        {
            ft_dprintf(STDERR_FILENO, "Parsing error: invalid piece index '%u'\n", (unsigned int)value);
            return -1;
        }

        np->board[yLen * np->size + xLen] = value;
        ptr += ft_strspn(ptr, " \t");
    }

    // Check if we have remaining stuff in the line buffer after parsing the whole row.
    if (*ptr != '#' && *ptr != '\0')
    {
        ft_dprintf(STDERR_FILENO, "Parsing error: extra data '%s' after piece indexes\n", ptr);
        return -1;
    }

    return 1;
}

int npuzzle_init(NPuzzle *np, const char *filename)
{
    FILE *f = fopen(filename, "r");

    if (f == NULL)
    {
        perror("Parsing error: unable to open n-puzzle file");
        return -1;
    }

    ssize_t r;
    char *line = NULL;
    size_t lineSize = 0;

    *np = (NPuzzle) {
        .size = 0,
        .board = NULL,
        .zobrist = 0,
        .holeIdx = 0,
        .h = 0,
        .g = 0,
        .parent = NULL
    };

    // Parse the size field of the file.
    while ((r = getline(&line, &lineSize, f)) > 0)
    {
        int retval = npuzzle_parse_size(np, line, (size_t)r);

        if (retval == -1)
            goto npuzzle_init_error;
        else if (retval == 1)
            break ;
    }

    if (np->size == 0 || np->size >= 256)
    {
        ft_dputstr("Parsing error: missing or invalid puzzle size\n", STDERR_FILENO);
        goto npuzzle_init_error;
    }

    np->board = malloc(2 * np->size * np->size);

    if (np->board == NULL)
    {
        perror("Parsing error");
        goto npuzzle_init_error;
    }

    size_t yLen = 0;

    // Parse the row fields of the file.
    while ((r = getline(&line, &lineSize, f)) > 0)
    {
        int retval = npuzzle_parse_row(np, line, r, yLen);

        if (retval == -1)
            goto npuzzle_init_error;
        else if (retval == 1)
            ++yLen;
    }

    // Check for duplicate pieces in the board.
    for (size_t sq1 = 0; sq1 < np->size * np->size; ++sq1)
        for (size_t sq2 = sq1 + 1; sq2 < np->size * np->size; ++sq2)
            if (np->board[sq1] == np->board[sq2])
            {
                ft_dputstr("NPuzzle Error: duplicate piece\n", STDERR_FILENO);
                goto npuzzle_init_error;
            }

    // Now that we know the board is valid, initialize other fields.
    size_t holeIdx;
    for (holeIdx = 0; np->board[holeIdx] != 0; ++holeIdx);
    np->holeIdx = holeIdx;
    np->zobrist = 0;

    // Edit array values to tag corresponding squares.
    uint16_t *tagArray = malloc(np->size * np->size * sizeof(uint16_t));

    if (tagArray == NULL)
    {
        perror("NPuzzle error");
        goto npuzzle_init_error;
    }

    npuzzle_init_tag_array(tagArray, np->size);

    // Now replace the piece values in the board by their corresponding square.
    for (size_t sq = 0; sq < np->size * np->size; ++sq)
        for (size_t asq = 0; asq < np->size * np->size; ++asq)
            if (np->board[sq] == tagArray[asq])
            {
                np->board[sq] = asq;
                break ;
            }

    free(tagArray);
    fclose(f);
    free(line);
    return 0;

npuzzle_init_error:
    free(np->board);
    fclose(f);
    free(line);
    return -1;
}

int npuzzle_init_rand(NPuzzle *np, size_t size)
{
    *np = (NPuzzle) {
        .size = size,
        .board = NULL,
        .zobrist = 0,
        .holeIdx = 0,
        .h = 0,
        .g = 0,
        .parent = NULL
    };

    np->board = malloc(sizeof(uint16_t) * size * size);
    uint16_t *tagArray = malloc(sizeof(uint16_t) * size * size);

    if (np->board == NULL || tagArray == NULL)
    {
        perror("Generation error");
        goto npuzzle_init_rand_error;
    }

    // Fill the board with its usual values.
    for (size_t i = 0; i < np->size * np->size; ++i)
        np->board[i] = (uint16_t)i;

    np->holeIdx = (np->size / 2) * np->size + (np->size - 1) / 2;

    // Intialize our pseudo-random number generator.
    uint64_t seed = time(NULL);

    // Start shuffling the pieces on the board.
    for (size_t i = 0; i < np->size * np->size * 8; ++i)
    {
        // Generate the next PRNG state.
        seed ^= seed << 13;
        seed ^= seed >> 7;
        seed ^= seed << 17;

        uint64_t nextMove = seed & 3;

        if (nextMove == 0 && np->holeIdx % np->size != 0)
            npuzzle_apply(np, np->holeIdx - 1);

        else if (nextMove == 1 && np->holeIdx % np->size != np->size - 1)
            npuzzle_apply(np, np->holeIdx + 1);

        else if (nextMove == 2 && np->holeIdx / np->size != 0)
            npuzzle_apply(np, np->holeIdx - np->size);

        else if (nextMove == 3 && np->holeIdx / np->size != np->size - 1)
            npuzzle_apply(np, np->holeIdx + np->size);
    }

    // Reset the zobrist hash and cost function for the initial state.
    np->zobrist = 0;
    np->g = 0;

    npuzzle_init_tag_array(tagArray, np->size);

    // Write the generated npuzzle state as a valid puzzle to stdout.
    printf("Puzzle state:\n\n%zu\n", np->size);

    int align = 1 + (size >= 4) + (size >= 10) + (size >= 32) + (size >= 100);

    for (size_t i = 0; i < np->size * np->size; ++i)
        printf("%*hu%c", align, tagArray[np->board[i]],
            i % np->size == np->size - 1 ? '\n' : ' ');

    puts("");
    fflush(stdout);

    free(tagArray);
    return 0;

npuzzle_init_rand_error:
    free(np->board);
    free(tagArray);
    return -1;
}

void npuzzle_destroy(NPuzzle *np)
{
    free(np->board);
    free(np);
}

void untyped_npuzzle_destroy(void *np)
{
    npuzzle_destroy(*(NPuzzle **)np);
}

int npuzzle_solved(const NPuzzle *np)
{
    for (size_t sq = 0; sq < np->size * np->size; ++sq)
        if (np->board[sq] != sq)
            return 0;

    return 1;
}

int npuzzle_is_solvable(const NPuzzle *np)
{
    size_t inversions = 0;

    for (size_t sq1 = 0; sq1 < np->size * np->size; ++sq1)
    {
        if (sq1 == np->holeIdx)
            continue ;

        for (size_t sq2 = sq1 + 1; sq2 < np->size * np->size; ++sq2)
            if (sq2 != np->holeIdx)
                inversions += (np->board[sq1] > np->board[sq2]);
    }

    if (np->size & 1)
        return !(inversions & 1);

    size_t parity = (np->size & 2) != 0;

    return ((np->holeIdx / np->size + parity) & 1) == (inversions & 1);
}

void npuzzle_apply(NPuzzle *np, uint16_t squareIdx)
{
    uint16_t piece = np->board[squareIdx];
    uint16_t holeValue = np->board[np->holeIdx];

    np->zobrist ^= move_zobrist(piece, squareIdx, np->holeIdx);
    np->board[np->holeIdx] = piece;
    np->board[squareIdx] = holeValue;
    np->holeIdx = squareIdx;
    np->g++;
}

NPuzzle *npuzzle_dup(NPuzzle *np)
{
    NPuzzle *new = malloc(sizeof(NPuzzle));

    if (new == NULL)
    {
        perror("NPuzzle error");
        return NULL;
    }

    new->size = np->size;
    new->board = malloc(2 * np->size * np->size);

    if (new->board == NULL)
    {
        perror("NPuzzle error");
        free(new);
        return NULL;
    }

    ft_memcpy(new->board, np->board, 2 * np->size * np->size);
    new->holeIdx = np->holeIdx;
    new->zobrist = np->zobrist;
    new->h = np->h;
    new->g = np->g;
    new->parent = np;
    return new;
}

int npuzzle_comp_state(const void *l, const void *r)
{
    const NPuzzle *left = l;
    const NPuzzle *right = r;

    if (left->zobrist < right->zobrist)
        return -1;
    else if (left->zobrist > right->zobrist)
        return 1;
    else
        return ft_memcmp(left->board, right->board, 2 * left->size * left->size);
}

int npuzzle_comp_stateptr(const void *l, const void *r)
{
    return npuzzle_comp_state(*(NPuzzle **)l, *(NPuzzle **)r);
}

int npuzzle_comp_value(const void *l, const void *r)
{
    extern uint64_t Weight;
    const NPuzzle *left = *(NPuzzle **)l;
    const NPuzzle *right = *(NPuzzle **)r;
    const uint64_t lvalue = node_value(left->h, left->g, Weight);
    const uint64_t rvalue = node_value(right->h, right->g, Weight);

    if (lvalue < rvalue)
        return -1;
    else if (lvalue > rvalue)
        return 1;
    else
        return (int)left->g - (int)right->g;
}
