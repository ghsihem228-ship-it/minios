#include "command.h"
int command_split(char *line, unsigned cap, char **argv, unsigned maximum) {
    if (!line || !argv || !cap)
        return -22;
    unsigned len = 0;
    while (len < cap && line[len])
        len++;
    if (len == cap)
        return -7;
    unsigned read = 0, write = 0, count = 0;
    while (read < len) {
        while (read < len && (line[read] == ' ' || line[read] == '\t'))
            read++;
        if (read == len)
            break;
        if (count == maximum)
            return -7;
        argv[count++] = line + write;
        char quote = 0;
        while (read < len) {
            char c = line[read++];
            if (!quote && (c == ' ' || c == '\t'))
                break;
            if (c == '\\' && quote != '\'') {
                if (read == len)
                    return -22;
                line[write++] = line[read++];
                continue;
            }
            if (c == '\'' || c == '"') {
                if (!quote) {
                    quote = c;
                    continue;
                }
                if (c == quote) {
                    quote = 0;
                    continue;
                }
            }
            line[write++] = c;
        }
        if (quote)
            return -22;
        line[write++] = 0;
    }
    return count;
}
