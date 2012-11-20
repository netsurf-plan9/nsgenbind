/* binding output generator for jsapi(spidermonkey) to libdom
 *
 * This file is part of nsgenbind.
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
#include "nsgenbind-ast.h"
#include "webidl-ast.h"
#include "jsapi-libdom.h"

#define HDR_COMMENT_SEP     "\n * \n * "
#define HDR_COMMENT_PREABLE "Generated by nsgenbind "


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


static int webidl_preamble_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;

	fprintf(binding->outfile, "%s", genbind_node_gettext(node));

	return 0;
}


static int webidl_hdrcomments_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;

	fprintf(binding->outfile,
		HDR_COMMENT_SEP"%s",
		genbind_node_gettext(node));

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

static int webidl_private_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;
	struct genbind_node *ident_node;
	struct genbind_node *type_node;


	ident_node = genbind_node_find_type(genbind_node_getnode(node),
					    NULL,
					    GENBIND_NODE_TYPE_IDENT);
	if (ident_node == NULL)
		return -1; /* bad AST */

	type_node = genbind_node_find_type(genbind_node_getnode(node),
					    NULL,
					    GENBIND_NODE_TYPE_STRING);
	if (type_node == NULL)
		return -1; /* bad AST */

	fprintf(binding->outfile,
		"        %s%s;\n",
		genbind_node_gettext(type_node),
		genbind_node_gettext(ident_node));

	return 0;
}

static int webidl_private_param_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;
	struct genbind_node *ident_node;
	struct genbind_node *type_node;


	ident_node = genbind_node_find_type(genbind_node_getnode(node),
					    NULL,
					    GENBIND_NODE_TYPE_IDENT);
	if (ident_node == NULL)
		return -1; /* bad AST */

	type_node = genbind_node_find_type(genbind_node_getnode(node),
					    NULL,
					    GENBIND_NODE_TYPE_STRING);
	if (type_node == NULL)
		return -1; /* bad AST */

	fprintf(binding->outfile,
		",\n\t\t%s%s",
		genbind_node_gettext(type_node),
		genbind_node_gettext(ident_node));

	return 0;
}

static int webidl_private_assign_cb(struct genbind_node *node, void *ctx)
{
	struct binding *binding = ctx;
	struct genbind_node *ident_node;
	const char *ident;

	ident_node = genbind_node_find_type(genbind_node_getnode(node),
					    NULL,
					    GENBIND_NODE_TYPE_IDENT);
	if (ident_node == NULL)
		return -1; /* bad AST */

	ident = genbind_node_gettext(ident_node);

	fprintf(binding->outfile, "\tprivate->%s = %s;\n", ident, ident);

	return 0;
}


static int
output_api_operations(struct binding *binding)
{
	int res = 0;

	/* finalise */
	if (binding->has_private) {
		/* finalizer with private to free */
		fprintf(binding->outfile,
			"static void jsclass_finalize(JSContext *cx, JSObject *obj)\n"
			"{\n"
			"\tstruct jsclass_private *private;\n"
			"\n"
			"\tprivate = JS_GetInstancePrivate(cx, obj, &JSClass_%s, NULL);\n",
			binding->interface);

		if (binding->finalise != NULL) {
			output_code_block(binding, genbind_node_getnode(binding->finalise));
		}

		fprintf(binding->outfile,
			"\tif (private != NULL) {\n"
			"\t\tfree(private);\n"
			"\t}\n"
			"}\n\n");
	} else if (binding->finalise != NULL) {
		/* finaliser without private data */
		fprintf(binding->outfile,
			"static void jsclass_finalize(JSContext *cx, JSObject *obj)\n"
			"{\n");

		output_code_block(binding, genbind_node_getnode(binding->finalise));

		fprintf(binding->outfile,
			"}\n\n");

	}

	if (binding->resolve != NULL) {
		/* generate resolver entry */
		fprintf(binding->outfile,
			"static JSBool jsclass_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp)\n"
			"{\n");

		output_code_block(binding, genbind_node_getnode(binding->resolve));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}

	if (binding->mark != NULL) {
		/* generate trace/mark entry */
		fprintf(binding->outfile,
			"static JSAPI_MARKOP(jsclass_mark)\n"
			"{\n");
		if(binding->has_private) {

			fprintf(binding->outfile,
				"\tstruct jsclass_private *private;\n"
				"\n"
				"\tprivate = JS_GetInstancePrivate(JSAPI_MARKCX, obj, &JSClass_%s, NULL);\n",
				binding->interface);
		}

		output_code_block(binding, genbind_node_getnode(binding->mark));

		fprintf(binding->outfile,
			"\treturn JS_TRUE;\n"
			"}\n\n");
	}
	return res;
}

void
output_code_block(struct binding *binding, struct genbind_node *codelist)
{
	struct genbind_node *code_node;

	code_node = genbind_node_find_type(codelist,
					   NULL,
					   GENBIND_NODE_TYPE_CBLOCK);
	if (code_node != NULL) {
		fprintf(binding->outfile,
			"%s\n",
			genbind_node_gettext(code_node));
	}
}

/** generate class initialiser which create the javascript class prototype */
static int
output_class_init(struct binding *binding)
{
	int res = 0;
	struct genbind_node *api_node;

	/* class Initialisor */
	fprintf(binding->outfile,
		"JSObject *jsapi_InitClass_%s(JSContext *cx, JSObject *parent)\n"
		"{\n"
		"\tJSObject *prototype;\n",
		binding->interface);

	api_node = genbind_node_find_type_ident(binding->gb_ast,
						      NULL,
						      GENBIND_NODE_TYPE_API,
						      "init");

	if (api_node != NULL) {
		output_code_block(binding, genbind_node_getnode(api_node));
	} else {
		fprintf(binding->outfile,
			"\n"
			"\tprototype = JS_InitClass(cx,\n"
			"\t\tparent,\n"
			"\t\tNULL,\n"
			"\t\t&JSClass_%s,\n"
			"\t\tNULL,\n"
			"\t\t0,\n"
			"\t\tNULL,\n"
			"\t\tNULL, \n"
			"\t\tNULL, \n"
			"\t\tNULL);\n",
			binding->interface);
	}

	output_const_defines(binding, binding->interface);

	fprintf(binding->outfile,
		"\treturn prototype;\n"
		"}\n\n");

	return res;
}

static int
output_class_new(struct binding *binding)
{
	int res = 0;
	struct genbind_node *api_node;

	/* constructor */
	fprintf(binding->outfile,
		"JSObject *jsapi_new_%s(JSContext *cx,\n"
		"\t\tJSObject *prototype,\n"
		"\t\tJSObject *parent",
		binding->interface);

	genbind_node_for_each_type(binding->binding_list,
				   GENBIND_NODE_TYPE_BINDING_PRIVATE,
				   webidl_private_param_cb,
				   binding);

	fprintf(binding->outfile,
		")\n"
		"{\n"
		"\tJSObject *newobject;\n");

	/* create private data */
	if (binding->has_private) {
		fprintf(binding->outfile,
			"\tstruct jsclass_private *private;\n"
			"\n"
			"\tprivate = malloc(sizeof(struct jsclass_private));\n"
			"\tif (private == NULL) {\n"
			"\t\treturn NULL;\n"
			"\t}\n");

		genbind_node_for_each_type(binding->binding_list,
					   GENBIND_NODE_TYPE_BINDING_PRIVATE,
					   webidl_private_assign_cb,
					   binding);
	}

	api_node = genbind_node_find_type_ident(binding->gb_ast,
						NULL,
						GENBIND_NODE_TYPE_API,
						"new");

	if (api_node != NULL) {
		output_code_block(binding, genbind_node_getnode(api_node));
	} else {
		fprintf(binding->outfile,
			"\n"
			"\tnewobject = JS_NewObject(cx, &JSClass_%s, prototype, parent);\n",
			binding->interface);
	}

	if (binding->has_private) {
		fprintf(binding->outfile,
			"\tif (newobject == NULL) {\n"
			"\t\tfree(private);\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");

		/* root object to stop it being garbage collected */
		fprintf(binding->outfile,
			"\tif (JSAPI_ADD_OBJECT_ROOT(cx, &newobject) != JS_TRUE) {\n"
			"\t\tfree(private);\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");

		fprintf(binding->outfile,
			"\n"
			"\t/* attach private pointer */\n"
			"\tif (JS_SetPrivate(cx, newobject, private) != JS_TRUE) {\n"
			"\t\tfree(private);\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");


		/* attach operations and attributes (functions and properties) */
		fprintf(binding->outfile,
			"\tif (JS_DefineFunctions(cx, newobject, jsclass_functions) != JS_TRUE) {\n"
			"\t\tfree(private);\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");

		fprintf(binding->outfile,
			"\tif (JS_DefineProperties(cx, newobject, jsclass_properties) != JS_TRUE) {\n"
			"\t\tfree(private);\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");
	} else {
		fprintf(binding->outfile,
			"\tif (newobject == NULL) {\n"
			"\t\treturn NULL;\n"
			"\t}\n");

		/* root object to stop it being garbage collected */
		fprintf(binding->outfile,
			"\tif (JSAPI_ADD_OBJECT_ROOT(cx, &newobject) != JS_TRUE) {\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");

		/* attach operations and attributes (functions and properties) */
		fprintf(binding->outfile,
			"\tif (JS_DefineFunctions(cx, newobject, jsclass_functions) != JS_TRUE) {\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");

		fprintf(binding->outfile,
			"\tif (JS_DefineProperties(cx, newobject, jsclass_properties) != JS_TRUE) {\n"
			"\t\treturn NULL;\n"
			"\t}\n\n");
	}
	
	/* unroot object and return it */
	fprintf(binding->outfile,
		"\tJSAPI_REMOVE_OBJECT_ROOT(cx, &newobject);\n"
		"\n"
		"\treturn newobject;\n"
		"}\n");


	return res;
}

static int
output_jsclass(struct binding *binding)
{
	if (binding->resolve != NULL) {
		/* forward declare the resolver */
		fprintf(binding->outfile,
			"static JSBool jsclass_resolve(JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp);\n\n");
	}

	if (binding->mark != NULL) {
		fprintf(binding->outfile,
			"static JSAPI_MARKOP(jsclass_mark);\n\n");
	}

	if (binding->has_private || (binding->finalise != NULL)) {

		/* forward declare the finalizer */
		fprintf(binding->outfile,
			"static void jsclass_finalize(JSContext *cx, JSObject *obj);\n\n");
	}

	/* output the class */
	fprintf(binding->outfile,
		"JSClass JSClass_%s = {\n"
		"\t\"%s\",\n",
		binding->interface, 
		binding->interface);

	/* generate class flags */
	if (binding->has_global) {
		fprintf(binding->outfile, "\tJSCLASS_GLOBAL_FLAGS");
	} else {
		fprintf(binding->outfile, "\t0");
	}

	if (binding->resolve != NULL) {
		fprintf(binding->outfile, " | JSCLASS_NEW_RESOLVE");
	}

	if (binding->mark != NULL) {
		fprintf(binding->outfile, " | JSAPI_JSCLASS_MARK_IS_TRACE");
	}

	if (binding->has_private) {
		fprintf(binding->outfile, " | JSCLASS_HAS_PRIVATE");
	}

	fprintf(binding->outfile, ",\n");

	/* stubs */
	fprintf(binding->outfile,
		"\tJS_PropertyStub,\t/* addProperty */\n"
		"\tJS_PropertyStub,\t/* delProperty */\n"
		"\tJS_PropertyStub,\t/* getProperty */\n"
		"\tJS_StrictPropertyStub,\t/* setProperty */\n"
		"\tJS_EnumerateStub,\t/* enumerate */\n");

	/* resolver */
	if (binding->resolve != NULL) {
		fprintf(binding->outfile, "\t(JSResolveOp)jsclass_resolve,\n");
	} else {
		fprintf(binding->outfile, "\tJS_ResolveStub,\n");
	}

	fprintf(binding->outfile, "\tJS_ConvertStub,\t/* convert */\n");

	if (binding->has_private || (binding->finalise != NULL)) {
		fprintf(binding->outfile, "\tjsclass_finalize,\n");
	} else {
		fprintf(binding->outfile, "\tJS_FinalizeStub,\n");
	}
	fprintf(binding->outfile,
		"\t0,\t/* reserved */\n"
		"\tNULL,\t/* checkAccess */\n"
		"\tNULL,\t/* call */\n"
		"\tNULL,\t/* construct */\n"
		"\tNULL,\t/* xdr Object */\n"
		"\tNULL,\t/* hasInstance */\n");

	/* trace/mark */
	if (binding->mark != NULL) {
		fprintf(binding->outfile, "\tJSAPI_JSCLASS_MARKOP(jsclass_mark),\n");
	} else {
		fprintf(binding->outfile, "\tNULL, /* trace/mark */\n");
	}

	fprintf(binding->outfile,
		"\tJSAPI_CLASS_NO_INTERNAL_MEMBERS\n"
		"};\n\n");
	return 0;
}

static int
output_private_declaration(struct binding *binding)
{
	struct genbind_node *type_node;

	if (!binding->has_private) {
		return 0;
	}

	type_node = genbind_node_find(binding->binding_list,
				      NULL,
				      genbind_cmp_node_type,
				      (void *)GENBIND_NODE_TYPE_TYPE);

	if (type_node == NULL) {
		return -1;
	}

	fprintf(binding->outfile, "struct jsclass_private {\n");

	genbind_node_for_each_type(binding->binding_list,
				   GENBIND_NODE_TYPE_BINDING_PRIVATE,
				   webidl_private_cb,
				   binding);

	genbind_node_for_each_type(binding->binding_list,
				   GENBIND_NODE_TYPE_BINDING_INTERNAL,
				   webidl_private_cb,
				   binding);

	fprintf(binding->outfile, "};\n\n");


	return 0;
}

static int
output_preamble(struct binding *binding)
{
	genbind_node_for_each_type(binding->gb_ast,
				   GENBIND_NODE_TYPE_PREAMBLE,
				   webidl_preamble_cb,
				   binding);

	fprintf(binding->outfile,"\n\n");

	return 0;
}

static int
output_header_comments(struct binding *binding)
{
	fprintf(binding->outfile, "/* "HDR_COMMENT_PREABLE);

	genbind_node_for_each_type(binding->gb_ast,
				   GENBIND_NODE_TYPE_HDRCOMMENT,
				   webidl_hdrcomment_cb,
				   binding);

	fprintf(binding->outfile,"\n */\n\n");
	return 0;
}

static bool
binding_has_private(struct genbind_node *binding_list)
{
	struct genbind_node *node;

	node = genbind_node_find_type(binding_list,
				      NULL,
				      GENBIND_NODE_TYPE_BINDING_PRIVATE);

	if (node != NULL) {
		return true;
	}

	node = genbind_node_find_type(binding_list,
				      NULL,
				      GENBIND_NODE_TYPE_BINDING_INTERNAL);
	if (node != NULL) {
		return true;
	}
	return false;
}

static bool
binding_has_global(struct binding *binding)
{
	struct genbind_node *api_node;

	api_node = genbind_node_find_type_ident(binding->gb_ast,
						NULL,
						GENBIND_NODE_TYPE_API,
						"global");
	if (api_node != NULL) {
		return true;
	}
	return false;
}

static struct binding *
binding_new(char *outfilename, struct genbind_node *genbind_ast)
{
	struct binding *nb;
	struct genbind_node *binding_node;
	struct genbind_node *binding_list;
	struct genbind_node *ident_node;
	struct genbind_node *interface_node;
	FILE *outfile ; /* output file */
	struct webidl_node *webidl_ast = NULL;
	int res;

	binding_node = genbind_node_find_type(genbind_ast,
					 NULL,
					 GENBIND_NODE_TYPE_BINDING);
	if (binding_node == NULL) {
		return NULL;
	}

	binding_list = genbind_node_getnode(binding_node);
	if (binding_list == NULL) {
		return NULL;
	}
	
	ident_node = genbind_node_find_type(binding_list,
					    NULL,
					    GENBIND_NODE_TYPE_IDENT);
	if (ident_node == NULL) {
		return NULL;
	}

	interface_node = genbind_node_find_type(binding_list,
					   NULL,
					   GENBIND_NODE_TYPE_BINDING_INTERFACE);
	if (interface_node == NULL) {
		return NULL;
	}

	/* walk ast and load any web IDL files required */
	res = read_webidl(genbind_ast, &webidl_ast);
	if (res != 0) {
		fprintf(stderr, "Error reading Web IDL files\n");
		return NULL;
	}

	/* open output file */
	if (outfilename == NULL) {
		outfile = stdout;
	} else {
		outfile = fopen(outfilename, "w");
	}
	if (outfile == NULL) {
		fprintf(stderr, "Error opening output %s: %s\n",
			outfilename,
			strerror(errno));
		return NULL;
	}

	nb = calloc(1, sizeof(struct binding));

	nb->gb_ast = genbind_ast;
	nb->wi_ast = webidl_ast;
	nb->name = genbind_node_gettext(ident_node);
	nb->interface = genbind_node_gettext(interface_node);
	nb->outfile = outfile;
	nb->has_private = binding_has_private(binding_list);
	nb->has_global = binding_has_global(nb);
	nb->binding_list = binding_list;
	nb->resolve = genbind_node_find_type_ident(genbind_ast,
						      NULL,
						      GENBIND_NODE_TYPE_API,
						      "resolve");
	nb->finalise = genbind_node_find_type_ident(genbind_ast,
						    NULL,
						    GENBIND_NODE_TYPE_API,
						    "finalise");

	nb->mark = genbind_node_find_type_ident(genbind_ast,
						    NULL,
						    GENBIND_NODE_TYPE_API,
						    "mark");
	return nb;
}


int jsapi_libdom_output(char *outfilename, struct genbind_node *genbind_ast)
{
	int res;
	struct binding *binding;

	/* get general binding information used in output */
	binding = binding_new(outfilename, genbind_ast);
	if (binding == NULL) {
		return 4;
	}

	res = output_header_comments(binding);
	if (res) {
		return 5;
	}

	res = output_preamble(binding);
	if (res) {
		return 6;
	}

	res = output_private_declaration(binding);
	if (res) {
		return 7;
	}

	res = output_jsclass(binding);
	if (res) {
		return 8;
	}

	res = output_operator_body(binding, binding->interface);
	if (res) {
		return 9;
	}

	res = output_property_body(binding, binding->interface);
	if (res) {
		return 10;
	}

	res = output_function_spec(binding);
	if (res) {
		return 11;
	}

	res = output_property_spec(binding);
	if (res) {
		return 12;
	}

	res = output_api_operations(binding);
	if (res) {
		return 13;
	}

	res = output_class_init(binding);
	if (res) {
		return 14;
	}

	res = output_class_new(binding);
	if (res) {
		return 15;
	}

	fclose(binding->outfile);

	return 0;
}
