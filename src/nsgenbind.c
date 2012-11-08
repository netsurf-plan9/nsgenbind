/* binding generator main and command line parsing
 *
 * This file is part of nsgenbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#include "nsgenbind-ast.h"
#include "jsapi-libdom.h"
#include "options.h"

struct options *options;

static struct options* process_cmdline(int argc, char **argv)
{
	int opt;

	options = calloc(1,sizeof(struct options));
	if (options == NULL) {
		fprintf(stderr, "Allocation error\n");
		return NULL;
	}

	while ((opt = getopt(argc, argv, "vDW::d:I:o:")) != -1) {
		switch (opt) {
		case 'I':
			options->idlpath = strdup(optarg);
			break;

		case 'o':
			options->outfilename = strdup(optarg);
			break;

		case 'd':
			options->depfilename = strdup(optarg);
			break;

		case 'v':
			options->verbose = true;
			break;

		case 'D':
			options->debug = true;
			break;

		case 'W':
			options->warnings = 1; /* warning flags */
			break;

		default: /* '?' */
			fprintf(stderr, 
			     "Usage: %s [-d depfilename] [-I idlpath] [-o filename] inputfile\n",
				argv[0]);
			free(options);
			return NULL;
                   
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Error: expected input filename\n");
		free(options);
		return NULL;
	}

	options->infilename = strdup(argv[optind]);

	return options;

}

int main(int argc, char **argv)
{
	int res;
	struct genbind_node *genbind_root;

	options = process_cmdline(argc, argv);
	if (options == NULL) {
		return 1; /* bad commandline */
	}

	if (options->verbose && 
	    (options->outfilename == NULL)) {
		fprintf(stderr, 
			"Error: output to stdout with verbose logging would fail\n");
		return 2;
	}

	if (options->depfilename != NULL &&
	    options->outfilename == NULL) {
		fprintf(stderr,
			"Error: output to stdout with dep generation would fail\n");
		return 3;
	}

	if (options->depfilename != NULL &&
	    options->infilename == NULL) {
		fprintf(stderr,
			"Error: input from stdin with dep generation would fail\n");
		return 3;
	}

	if (options->depfilename != NULL) {
		options->depfilehandle = fopen(options->depfilename, "w");
		if (options->depfilehandle == NULL) {
			fprintf(stderr,
				"Error: unable to open dep file\n");
			return 4;
		}
		fprintf(options->depfilehandle,
			"%s %s :", options->depfilename,
			options->outfilename);
	}

	res = genbind_parsefile(options->infilename, &genbind_root);
	if (res != 0) {
		fprintf(stderr, "Error: parse failed with code %d\n", res);
		return res;
	}

	if (options->verbose) {
		genbind_ast_dump(genbind_root, 0);
	}

	res = jsapi_libdom_output(options->outfilename, genbind_root);
	if (res != 0) {
		fprintf(stderr, "Error: output failed with code %d\n", res);
		unlink(options->outfilename);
		return res;
	}

	if (options->depfilehandle != NULL) {
		fputc('\n', options->depfilehandle);
		fclose(options->depfilehandle);
	}

	return 0;
} 
