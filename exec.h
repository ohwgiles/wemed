#ifndef EXEC_H
#define EXEC_H
/* Copyright 2013 Oliver Giles
 * This file is part of Wemed. Wemed is licensed under the
 * GNU GPL version 3. See LICENSE or <http://www.gnu.org/licenses/>
 * for more information */
#include <unistd.h>

ssize_t exec_get(char* buffer, size_t nbuf, const char* file, const char* const* args);

#endif

