/* -*- c-file-style: "ruby" -*- */
/*
  Copyright (C) 2009  Kouhei Sutou <kou@clear-code.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License version 2.1 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "rb-grn.h"

#define SELF(object) (RVAL2GRNCONTEXT(object))

#define OBJECTS_TABLE_NAME "<ranguba:objects>"

static VALUE cGrnContext;

/*
 * Document-class: Groonga::Context
 *
 * groonga全体に渡る情報を管理するオブジェクト。通常のアプリ
 * ケーションでは1つのコンテキストを作成し、それを利用する。
 * 複数のコンテキストを利用する必要はない。
 *
 * デフォルトで使用されるコンテキストは
 * Groonga::Context#defaultでアクセスできる。コンテキストを
 * 指定できる箇所でコンテキストの指定を省略したり+nil+を指定
 * した場合はGroonga::Context.defaultが利用される。
 *
 * また、デフォルトのコンテキストは必要になると暗黙のうちに
 * 作成される。そのため、コンテキストを意識することは少ない。
 *
 * 暗黙のうちに作成されるコンテキストにオプションを指定する
 * 場合はGroonga::Context.default_options=を使用する。
 */

grn_ctx *
rb_grn_context_from_ruby_object (VALUE object)
{
    grn_ctx *context;

    if (!RVAL2CBOOL(rb_obj_is_kind_of(object, cGrnContext))) {
	rb_raise(rb_eTypeError, "not a groonga context");
    }

    Data_Get_Struct(object, grn_ctx, context);
    if (!context)
	rb_raise(rb_eGrnError, "groonga context is NULL");
    return context;
}

void
rb_grn_context_register (grn_ctx *context, RbGrnObject *object)
{
    grn_obj *objects;

    if (!grn_ctx_db(context))
	return;

    objects = grn_ctx_get(context,
			  OBJECTS_TABLE_NAME,
			  strlen(OBJECTS_TABLE_NAME));
    if (!objects)
	objects = grn_table_create(context,
				   OBJECTS_TABLE_NAME,
				   strlen(OBJECTS_TABLE_NAME),
				   NULL,
				   GRN_OBJ_TABLE_HASH_KEY,
				   grn_ctx_at(context, GRN_DB_UINT64),
				   0);

    grn_table_add(context, objects,
		  &object, sizeof(RbGrnObject *),
		  NULL);
}

void
rb_grn_context_unregister (grn_ctx *context, RbGrnObject *object)
{
    grn_obj *objects;

    if (!grn_ctx_db(context))
	return;

    objects = grn_ctx_get(context,
			  OBJECTS_TABLE_NAME,
			  strlen(OBJECTS_TABLE_NAME));
    if (!objects)
	return;

    grn_table_delete(context, objects, &object, sizeof(RbGrnObject *));
}

void
rb_grn_context_unbind (grn_ctx *context)
{
    grn_obj *database;
    grn_obj *objects;

    database = grn_ctx_db(context);
    if (!database)
	return;

    objects = grn_ctx_get(context,
			  OBJECTS_TABLE_NAME,
			  strlen(OBJECTS_TABLE_NAME));
    if (objects) {
	grn_table_cursor *cursor;

	cursor = grn_table_cursor_open(context, objects, NULL, 0, NULL, 0, 0);
	while (grn_table_cursor_next(context, cursor) != GRN_ID_NIL) {
	    void *value;
	    RbGrnObject *object = NULL;
	    grn_table_cursor_get_key(context, cursor, &value);
	    memcpy(&object, value, sizeof(RbGrnObject *));
	    object->object = NULL;
	    object->unbind(object);
	}
	grn_table_cursor_close(context, cursor);
	grn_obj_close(context, objects);
    }

    grn_obj_close(context, database);
    grn_ctx_use(context, NULL);
}

static void
rb_grn_context_free (void *pointer)
{
    grn_ctx *context = pointer;

    if (context->stat != GRN_CTX_FIN) {
	rb_grn_context_unbind (context);
	grn_ctx_fin(context);
    }
    xfree(context);
}

static VALUE
rb_grn_context_alloc (VALUE klass)
{
    return Data_Wrap_Struct(klass, NULL, rb_grn_context_free, NULL);
}

VALUE
rb_grn_context_to_exception (grn_ctx *context, VALUE related_object)
{
    VALUE exception, exception_class;
    const char *message;
    grn_obj bulk;

    if (context->rc == GRN_SUCCESS)
	return Qnil;

    exception_class = rb_grn_rc_to_exception(context->rc);
    message = rb_grn_rc_to_message(context->rc);

    GRN_OBJ_INIT(&bulk, GRN_BULK, 0, GRN_ID_NIL);
    GRN_TEXT_PUTS(context, &bulk, message);
    GRN_TEXT_PUTS(context, &bulk, ": ");
    GRN_TEXT_PUTS(context, &bulk, context->errbuf);
    if (!NIL_P(related_object)) {
	GRN_TEXT_PUTS(context, &bulk, ": ");
	GRN_TEXT_PUTS(context, &bulk, rb_grn_inspect(related_object));
    }
    GRN_TEXT_PUTS(context, &bulk, "\n");
    GRN_TEXT_PUTS(context, &bulk, context->errfile);
    GRN_TEXT_PUTS(context, &bulk, ":");
    grn_text_itoa(context, &bulk, context->errline);
    GRN_TEXT_PUTS(context, &bulk, ": ");
    GRN_TEXT_PUTS(context, &bulk, context->errfunc);
    GRN_TEXT_PUTS(context, &bulk, "()");
    exception = rb_funcall(exception_class, rb_intern("new"), 1,
			   rb_str_new(GRN_BULK_HEAD(&bulk),
				      GRN_BULK_VSIZE(&bulk)));
    grn_obj_close(context, &bulk);

    return exception;
}

void
rb_grn_context_check (grn_ctx *context, VALUE related_object)
{
    VALUE exception;

    exception = rb_grn_context_to_exception(context, related_object);
    if (NIL_P(exception))
	return;

    rb_exc_raise(exception);
}

grn_ctx *
rb_grn_context_ensure (VALUE *context)
{
    if (NIL_P(*context))
	*context = rb_grn_context_get_default();
    return SELF(*context);
}

/*
 * call-seq:
 *   Groonga::Context.default -> Groonga::Context
 *
 * デフォルトのコンテキストを返す。デフォルトのコンテキスト
 * が作成されていない場合は暗黙のうちに作成し、それを返す。
 *
 * 暗黙のうちにコンテキストを作成する場合は、
 * Groonga::Context.default_optionsに設定されているオプショ
 * ンを利用する。
 */
static VALUE
rb_grn_context_s_get_default (VALUE self)
{
    VALUE context;

    context = rb_cv_get(self, "@@default");
    if (NIL_P(context)) {
	context = rb_funcall(cGrnContext, rb_intern("new"), 0);
	rb_cv_set(self, "@@default", context);
    }
    return context;
}

VALUE
rb_grn_context_get_default (void)
{
    return rb_grn_context_s_get_default(cGrnContext);
}

/*
 * call-seq:
 *   Groonga::Context.default=(context)
 *
 * デフォルトのコンテキストを設定する。+nil+を指定すると、デ
 * フォルトのコンテキストをリセットする。リセットすると、次
 * 回Groonga::Context.defaultを呼び出したときに新しくコンテ
 * キストが作成される。
 */
static VALUE
rb_grn_context_s_set_default (VALUE self, VALUE context)
{
    rb_cv_set(self, "@@default", context);
    return Qnil;
}

/*
 * call-seq:
 *   Groonga::Context.default_options -> Hash or nil
 *
 * コンテキストを作成する時に利用するデフォルトのオプション
 * を返す。
 */
static VALUE
rb_grn_context_s_get_default_options (VALUE self)
{
    return rb_cv_get(self, "@@default_options");
}

/*
 * call-seq:
 *   Groonga::Context.default_options=(options)
 *
 * コンテキストを作成する時に利用するデフォルトのオプション
 * を設定する。利用可能なオプションは
 * Groonga::Context.newを参照。
 */
static VALUE
rb_grn_context_s_set_default_options (VALUE self, VALUE options)
{
    rb_cv_set(self, "@@default_options", options);
    return Qnil;
}

/*
 * call-seq:
 *   Groonga::Context.new(options=nil)
 *
 * コンテキストを作成する。_options_に指定可能な値は以下の通
 * り。
 *
 * [+:encoding+]
 *   エンコーディングを指定する。エンコーディングの指定方法
 *   はGroonga::Encodingを参照。
 */
static VALUE
rb_grn_context_initialize (int argc, VALUE *argv, VALUE self)
{
    grn_ctx *context;
    int flags = 0;
    grn_rc rc;
    VALUE options, default_options;
    VALUE rb_encoding;

    rb_scan_args(argc, argv, "01", &options);
    default_options = rb_grn_context_s_get_default_options(rb_obj_class(self));
    if (NIL_P(default_options))
	default_options = rb_hash_new();

    if (NIL_P(options))
	options = rb_hash_new();
    options = rb_funcall(default_options, rb_intern("merge"), 1, options);

    rb_grn_scan_options(options,
			"encoding", &rb_encoding,
			NULL);

    context = ALLOC(grn_ctx);
    DATA_PTR(self) = context;
    rc = grn_ctx_init(context, flags);
    rb_grn_context_check(context, self);

    if (!NIL_P(rb_encoding)) {
	grn_encoding encoding;

	encoding = RVAL2GRNENCODING(rb_encoding, NULL);
	GRN_CTX_SET_ENCODING(context, encoding);
    }

    return Qnil;
}

/*
 * call-seq:
 *   context.inspect -> String
 *
 * コンテキストの中身を人に見やすい文字列で返す。
 */
static VALUE
rb_grn_context_inspect (VALUE self)
{
    VALUE inspected;
    grn_ctx *context;
    grn_obj *database;
    VALUE rb_database;

    context = SELF(self);

    inspected = rb_str_new2("#<");
    rb_str_concat(inspected, rb_inspect(rb_obj_class(self)));
    rb_str_cat2(inspected, " ");

    rb_str_cat2(inspected, "encoding: <");
    rb_str_concat(inspected, rb_inspect(GRNENCODING2RVAL(context->encoding)));
    rb_str_cat2(inspected, ">, ");

    rb_str_cat2(inspected, "database: <");
    database = grn_ctx_db(context);
    rb_database = GRNDB2RVAL(context, database, RB_GRN_FALSE);
    rb_str_concat(inspected, rb_inspect(rb_database));
    rb_str_cat2(inspected, ">");

    rb_str_cat2(inspected, ">");
    return inspected;
}

/*
 * call-seq:
 *   context.encoding -> Groonga::Encoding
 *
 * コンテキストが使うエンコーディングを返す。
 */
static VALUE
rb_grn_context_get_encoding (VALUE self)
{
    return GRNENCODING2RVAL(GRN_CTX_GET_ENCODING(SELF(self)));
}

/*
 * call-seq:
 *   context.encoding=(encoding)
 *
 * コンテキストが使うエンコーディングを設定する。エンコーディ
 * ングの指定のしかたはGroonga::Encodingを参照。
 */
static VALUE
rb_grn_context_set_encoding (VALUE self, VALUE rb_encoding)
{
    grn_ctx *context;
    grn_encoding encoding;

    context = SELF(self);
    encoding = RVAL2GRNENCODING(rb_encoding, NULL);
    GRN_CTX_SET_ENCODING(context, encoding);

    return rb_encoding;
}

/*
 * call-seq:
 *   context.database -> Groonga::Database
 *
 * コンテキストが使うデータベースを返す。
 */
static VALUE
rb_grn_context_get_database (VALUE self)
{
    grn_ctx *context;

    context = SELF(self);
    return GRNDB2RVAL(context, grn_ctx_db(context), RB_GRN_FALSE);
}

/*
 * call-seq:
 *   context[name] -> Groonga::Object or nil
 *   context[id]   -> Groonga::Object or nil
 *
 * コンテキスト管理下にあるオブジェクトを返す。
 *
 * _name_として文字列を指定した場合はオブジェクト名でオブジェ
 * クトを検索する。
 *
 * _id_として数値を指定した場合はオブジェクトIDでオブジェク
 * トを検索する。
 */
static VALUE
rb_grn_context_array_reference (VALUE self, VALUE name_or_id)
{
    grn_ctx *context;
    grn_obj *object;

    context = SELF(self);
    if (RVAL2CBOOL(rb_obj_is_kind_of(name_or_id, rb_cString))) {
	const char *name;
	unsigned name_size;

	name = StringValuePtr(name_or_id);
	name_size = RSTRING_LEN(name_or_id);
	object = grn_ctx_get(context, name, name_size);
    } else if (RVAL2CBOOL(rb_obj_is_kind_of(name_or_id, rb_cInteger))) {
	unsigned id;
	id = NUM2UINT(name_or_id);
	object = grn_ctx_at(context, id);
    } else {
	rb_raise(rb_eArgError, "should be string or unsigned integer: %s",
		 rb_grn_inspect(name_or_id));
    }

    return GRNOBJECT2RVAL(Qnil, context, object, RB_GRN_FALSE);
}

void
rb_grn_init_context (VALUE mGrn)
{
    cGrnContext = rb_define_class_under(mGrn, "Context", rb_cObject);
    rb_define_alloc_func(cGrnContext, rb_grn_context_alloc);

    rb_cv_set(cGrnContext, "@@default", Qnil);
    rb_cv_set(cGrnContext, "@@default_options", Qnil);

    rb_define_singleton_method(cGrnContext, "default",
			       rb_grn_context_s_get_default, 0);
    rb_define_singleton_method(cGrnContext, "default=",
			       rb_grn_context_s_set_default, 1);
    rb_define_singleton_method(cGrnContext, "default_options",
			       rb_grn_context_s_get_default_options, 0);
    rb_define_singleton_method(cGrnContext, "default_options=",
			       rb_grn_context_s_set_default_options, 1);

    rb_define_method(cGrnContext, "initialize", rb_grn_context_initialize, -1);

    rb_define_method(cGrnContext, "inspect", rb_grn_context_inspect, 0);

    rb_define_method(cGrnContext, "encoding", rb_grn_context_get_encoding, 0);
    rb_define_method(cGrnContext, "encoding=", rb_grn_context_set_encoding, 1);

    rb_define_method(cGrnContext, "database", rb_grn_context_get_database, 0);

    rb_define_method(cGrnContext, "[]", rb_grn_context_array_reference, 1);
}
