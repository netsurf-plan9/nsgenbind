/* binding output generator for jsapi(spidermonkey) to libdom
 *
 * This file is part of nsgenjsbind.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2012 Vincent Sanders <vince@netsurf-browser.org>
 */

#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "options.h"
#include "genjsbind-ast.h"
#include "webidl-ast.h"
#include "jsapi-libdom.h"

#define HDR_COMMENT_SEP     "\n * "
#define HDR_COMMENT_PREABLE "Generated by nsgenjsapi"

struct binding {
	const char *name; /* name of the binding */
	const char *interface; /* webidl interface binding is for */
};

static int webidl_preamble_cb(struct genbind_node *node, void *ctx)
{
	FILE *outfile = ctx;
	char *txt;
	txt = genbind_node_gettext(node);
	fprintf(outfile, "%s", txt);
	return 0;
}

static int
output_preamble(FILE *outfile, struct genbind_node *genbind_ast)
{
	genbind_node_for_each_type(genbind_ast,
				   GENBIND_NODE_TYPE_PREAMBLE,
				   webidl_preamble_cb,
				   outfile);
	return 0;
}

static int webidl_hdrcomments_cb(struct genbind_node *node, void *ctx)
{
	FILE *outfile = ctx;
	char *txt;
	txt = genbind_node_gettext(node);
	fprintf(outfile, HDR_COMMENT_SEP"%s",txt);
	return 0;
}

static int webidl_hdrcomment_cb(struct genbind_node *node, void *ctx)
{
	genbind_node_for_each_type(genbind_node_getnode(node),
				   GENBIND_NODE_TYPE_STRING,
				   webidl_hdrcomments_cb,
				   ctx);
	return 0;
}

static int
output_header_comments(FILE *outfile, struct genbind_node *genbind_ast)
{
	fprintf(outfile, "/* "HDR_COMMENT_PREABLE);
	genbind_node_for_each_type(genbind_ast,
				   GENBIND_NODE_TYPE_HDRCOMMENT,
				   webidl_hdrcomment_cb,
				   outfile);
	fprintf(outfile,"\n */\n\n");
	return 0;
}

static int webidl_file_cb(struct genbind_node *node, void *ctx)
{
	struct webidl_node **webidl_ast = ctx;
	char *filename;

	filename = genbind_node_gettext(node);

	return webidl_parsefile(filename, webidl_ast);

}

static int
read_webidl(struct genbind_node *genbind_ast, struct webidl_node **webidl_ast)
{
	int res;

	res = genbind_node_for_each_type(genbind_ast,
					 GENBIND_NODE_TYPE_WEBIDLFILE,
					 webidl_file_cb,
					 webidl_ast);

	/* debug dump of web idl AST */
	if (options->verbose) {
		webidl_ast_dump(*webidl_ast, 0);
	}
	return res;
}




static int webidl_property_spec_cb(struct webidl_node *node, void *ctx)
{
	FILE *outfile = ctx;
	struct webidl_node *ident_node;

	ident_node = webidl_node_find(webidl_node_getnode(node),
				      NULL,
				      webidl_cmp_node_type,
				      (void *)WEBIDL_NODE_TYPE_IDENT);

	if (ident_node == NULL) {
		/* properties must have an operator 
		* http://www.w3.org/TR/WebIDL/#idl-attributes
		*/
		return 1;
	} else {
		fprintf(outfile,
			"    JSAPI_PS(%s, 0, JSPROP_ENUMERATE | JSPROP_SHARED),\n",
			webidl_node_gettext(ident_node));
	}
	return 0;
}

static int
generate_property_spec(FILE *outfile,
		       const char *interface,
		       struct webidl_node *webidl_ast)
{
	struct webidl_node *interface_node;
	struct webidl_node *members_node;
	struct webidl_node *inherit_node;


	/* find interface in webidl with correct ident attached */
	interface_node = webidl_node_find_type_ident(webidl_ast,
						     WEBIDL_NODE_TYPE_INTERFACE,
						     interface);

	if (interface_node == NULL) {
		fprintf(stderr,
			"Unable to find interface %s in loaded WebIDL\n",
			interface);
		return -1;
	}

	members_node = webidl_node_find(webidl_node_getnode(interface_node),
					NULL,
					webidl_cmp_node_type,
					(void *)WEBIDL_NODE_TYPE_INTERFACE_MEMBERS);

	while (members_node != NULL) {

		fprintf(outfile,"    /**** %s ****/\n", interface);


		/* for each function emit a JSAPI_FS()*/
		webidl_node_for_each_type(webidl_node_getnode(members_node),
					  WEBIDL_NODE_TYPE_ATTRIBUTE,
					  webidl_property_spec_cb,
					  outfile);


		members_node = webidl_node_find(webidl_node_getnode(interface_node),
						members_node,
						webidl_cmp_node_type,
						(void *)WEBIDL_NODE_TYPE_INTERFACE_MEMBERS);

	}
	/* check for inherited nodes and insert them too */
	inherit_node = webidl_node_find(webidl_node_getnode(interface_node),
					NULL,
					webidl_cmp_node_type,
					(void *)WEBIDL_NODE_TYPE_INTERFACE_INHERITANCE);

	if (inherit_node != NULL) {
		return generate_property_spec(outfile, 
					      webidl_node_gettext(inherit_node),
					      webidl_ast);
	}

	return 0;
}

static int
output_property_spec(FILE *outfile,
		     struct binding *binding,
		       struct webidl_node *webidl_ast)
{
	int res;
	fprintf(outfile,
		"static JSPropertySpec jsproperties_%s[] = {\n",
		binding->name);

	res = generate_property_spec(outfile, binding->interface, webidl_ast);

	fprintf(outfile, "    JSAPI_PS_END\n};\n");

	return res;
}


static int webidl_func_spec_cb(struct webidl_node *node, void *ctx)
{
	FILE *outfile = ctx;
	struct webidl_node *ident_node;

	ident_node = webidl_node_find(webidl_node_getnode(node),
				      NULL,
				      webidl_cmp_node_type,
				      (void *)WEBIDL_NODE_TYPE_IDENT);

	if (ident_node == NULL) {
		/* operation without identifier - must have special keyword
		 * http://www.w3.org/TR/WebIDL/#idl-operations
		 */
	} else {
		fprintf(outfile,
			"    JSAPI_FS(%s, 0, 0),\n",
			webidl_node_gettext(ident_node));
	}
	return 0;
}

static int
generate_function_spec(FILE *outfile,
		       const char *interface,
		       struct webidl_node *webidl_ast)
{
	struct webidl_node *interface_node;
	struct webidl_node *members_node;
	struct webidl_node *inherit_node;


	/* find interface in webidl with correct ident attached */
	interface_node = webidl_node_find_type_ident(webidl_ast,
						     WEBIDL_NODE_TYPE_INTERFACE,
						     interface);

	if (interface_node == NULL) {
		fprintf(stderr,
			"Unable to find interface %s in loaded WebIDL\n",
			interface);
		return -1;
	}

	members_node = webidl_node_find(webidl_node_getnode(interface_node),
					NULL,
					webidl_cmp_node_type,
					(void *)WEBIDL_NODE_TYPE_INTERFACE_MEMBERS);
	while (members_node != NULL) { 

		fprintf(outfile,"    /**** %s ****/\n", interface);

		/* for each function emit a JSAPI_FS()*/
		webidl_node_for_each_type(webidl_node_getnode(members_node),
					  WEBIDL_NODE_TYPE_OPERATION,
					  webidl_func_spec_cb,
					  outfile);

		members_node = webidl_node_find(webidl_node_getnode(interface_node),
						members_node,
						webidl_cmp_node_type,
						(void *)WEBIDL_NODE_TYPE_INTERFACE_MEMBERS);
	}

	/* check for inherited nodes and insert them too */
	inherit_node = webidl_node_find(webidl_node_getnode(interface_node),
					NULL,
					webidl_cmp_node_type,
					(void *)WEBIDL_NODE_TYPE_INTERFACE_INHERITANCE);

	if (inherit_node != NULL) {
		return generate_function_spec(outfile, 
					      webidl_node_gettext(inherit_node),
					      webidl_ast);
	}

	return 0;
}

static int
output_function_spec(FILE *outfile,
		     struct binding *binding,
		       struct webidl_node *webidl_ast)
{
	int res;
	fprintf(outfile,
		"static JSFunctionSpec jsfunctions_%s[] = {\n",
		binding->name);

	res = generate_function_spec(outfile, binding->interface, webidl_ast);

	fprintf(outfile, "   JSAPI_FS_END\n};\n");

	return res;
}

static struct binding *binding_new(struct genbind_node *genbind_ast)
{
	struct binding *nb;
	struct genbind_node *binding_node;
	struct genbind_node *ident_node;
	struct genbind_node *interface_node;

	binding_node = genbind_node_find(genbind_ast,
					 NULL,
					 genbind_cmp_node_type,
					 (void *)GENBIND_NODE_TYPE_BINDING);

	if (binding_node == NULL) {
		return NULL;
	}

	ident_node = genbind_node_find(genbind_node_getnode(binding_node),
				       NULL,
				       genbind_cmp_node_type,
				       (void *)GENBIND_NODE_TYPE_IDENT);

	if (ident_node == NULL) {
		return NULL;
	}

	interface_node = genbind_node_find(genbind_node_getnode(binding_node),
				       NULL,
				       genbind_cmp_node_type,
				       (void *)GENBIND_NODE_TYPE_TYPE_INTERFACE);

	if (interface_node == NULL) {
		return NULL;
	}

	nb = calloc(1, sizeof(struct binding));

	nb->name = genbind_node_gettext(ident_node);
	nb->interface = genbind_node_gettext(interface_node);

	return nb;
}

int jsapi_libdom_output(char *outfilename, struct genbind_node *genbind_ast)
{
	FILE *outfile = NULL;
	struct webidl_node *webidl_ast = NULL;
	int res;
	struct binding *binding;

	/* walk ast and load any web IDL files required */
	res = read_webidl(genbind_ast, &webidl_ast);
	if (res != 0) {
		fprintf(stderr, "Error reading Web IDL files\n");
		return 5;
	}

	/* get general binding information used in output */
	binding = binding_new(genbind_ast);

	/* open output file */
	if (outfilename == NULL) {
		outfile = stdout;
	} else {
		outfile = fopen(outfilename, "w");
	}

	if (!outfile) {
		fprintf(stderr, "Error opening output %s: %s\n",
			outfilename,
			strerror(errno));
		return 4;
	}

	output_header_comments(outfile, genbind_ast);

	output_preamble(outfile, genbind_ast);

	res = output_function_spec(outfile, binding, webidl_ast);
	if (res) {
		return 5;
	}

	res = output_property_spec(outfile, binding, webidl_ast);
	if (res) {
		return 5;
	}

	fclose(outfile);

	return 0;
}
