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

#define SELF(object) (RVAL2GRNTABLE(object))
#define RVAL2GRNKEY(object, context, domain, related_object) \
    (rb_grn_key_from_ruby_object(object, context, domain, related_object))

VALUE rb_mGrnTableKeySupport;

/* FIXME */
grn_rc grn_table_get_info(grn_ctx *ctx, grn_obj *table, grn_obj_flags *flags,
                          grn_encoding *encoding, grn_obj **tokenizer);

static grn_obj *
rb_grn_key_from_ruby_object (VALUE rb_key, grn_ctx *context, grn_id domain_id,
			     VALUE related_object)
{
    grn_obj *key;
    grn_obj *domain = NULL;
    grn_id id;

    if (domain_id != GRN_ID_NIL)
	domain = grn_ctx_get(context, domain_id);

    if (!domain)
	return RVAL2GRNBULK(context, rb_key);

    switch (domain->header.type) {
      case GRN_TYPE:
	return RVAL2GRNBULK_WITH_TYPE(context, rb_key, domain_id);
	break;
      case GRN_TABLE_HASH_KEY:
      case GRN_TABLE_PAT_KEY:
      case GRN_TABLE_NO_KEY:
	id = RVAL2GRNID(rb_key, context, domain, related_object);
	break;
      default:
	if (!RVAL2CBOOL(rb_obj_is_kind_of(rb_key, rb_cInteger)))
	    rb_raise(rb_eGrnError,
		     "should be unsigned integer: <%s>: <%s>",
		     rb_grn_inspect(rb_key),
		     rb_grn_inspect(related_object));

	id = NUM2UINT(rb_key);
	break;
    }

    key = grn_obj_open(context, GRN_BULK, 0, GRN_ID_NIL);
    GRN_BULK_SET(context, key, &id, sizeof(id));
    return key;
}

static grn_id
rb_grn_table_key_support_add_raw (VALUE self, VALUE rb_key,
				  grn_ctx *context, grn_obj *table)
{
    VALUE exception;
    grn_id id;
    grn_obj *key;
    grn_search_flags flags;

    key = RVAL2GRNKEY(rb_key, context, table->header.domain, self);
    flags = GRN_SEARCH_EXACT | GRN_TABLE_ADD;
    id = grn_table_lookup(context, table,
			  GRN_BULK_HEAD(key), GRN_BULK_VSIZE(key),
			  &flags);
    exception = rb_grn_context_to_exception(context, self);
    grn_obj_close(context, key);
    if (!NIL_P(exception))
	rb_exc_raise(exception);

    return id;
}

static VALUE
rb_grn_table_key_support_add (VALUE self, VALUE rb_key)
{
    grn_ctx *context;
    grn_obj *table;
    grn_id id;

    context = rb_grn_object_ensure_context(self, Qnil);
    table = SELF(self);

    id = rb_grn_table_key_support_add_raw(self, rb_key, context, table);
    if (GRN_ID_NIL == id)
	return Qnil;
    else
	return rb_grn_record_new(self, id);
}

static VALUE
rb_grn_table_key_support_get_key (VALUE self, VALUE rb_id)
{
    grn_ctx *context;
    grn_id id;
    grn_obj *table, *key;
    int key_size = 0;
    VALUE rb_key;

    context = rb_grn_object_ensure_context(self, Qnil);
    table = SELF(self);

    id = NUM2UINT(rb_id);
    key_size = grn_table_get_key(context, table, id, NULL, 0);
    if (key_size == 0)
	return Qnil;

    key = grn_obj_open(context, GRN_BULK, 0, GRN_ID_NIL);
    grn_bulk_space(context, key, key_size);
    grn_table_get_key(context, SELF(self), id, GRN_BULK_HEAD(key), key_size);
    rb_key = GRNKEY2RVAL(context, GRN_BULK_HEAD(key), key_size, table, self);
    grn_obj_close(context, key);

    return rb_key;
}

static VALUE
rb_grn_table_key_support_delete_by_key (VALUE self, VALUE rb_key)
{
    VALUE exception;
    grn_ctx *context;
    grn_obj *table;
    grn_obj *key;
    grn_rc rc;

    context = rb_grn_object_ensure_context(self, Qnil);
    table = SELF(self);

    key = RVAL2GRNKEY(rb_key, context, table->header.domain, self);
    rc = grn_table_delete(context, table,
			  GRN_BULK_HEAD(key), GRN_BULK_VSIZE(key));
    exception = rb_grn_context_to_exception(context, self);
    grn_obj_close(context, key);
    if (!NIL_P(exception))
	rb_exc_raise(exception);
    rb_grn_rc_check(rc, self);

    return Qnil;
}

static VALUE
rb_grn_table_key_support_delete (VALUE self, VALUE rb_id_or_key)
{
    if (RVAL2CBOOL(rb_obj_is_kind_of(rb_id_or_key, rb_cFixnum))) {
	int argc = 1;
	VALUE argv[1];
	argv[0] = rb_id_or_key;
	return rb_call_super(argc, argv);
    } else {
	return rb_grn_table_key_support_delete_by_key(self, rb_id_or_key);
    }
}

static VALUE
rb_grn_table_key_support_array_reference_by_key (VALUE self, VALUE rb_key)
{
    VALUE exception;
    grn_ctx *context;
    grn_obj *table;
    grn_obj *key;
    grn_id id;
    grn_search_flags flags = 0;

    context = rb_grn_object_ensure_context(self, Qnil);
    table = SELF(self);

    key = RVAL2GRNKEY(rb_key, context, table->header.domain, self);
    flags = GRN_SEARCH_EXACT;
    id = grn_table_lookup(context, table,
			  GRN_BULK_HEAD(key), GRN_BULK_VSIZE(key),
			  &flags);
    exception = rb_grn_context_to_exception(context, self);
    grn_obj_close(context, key);
    if (!NIL_P(exception))
	rb_exc_raise(exception);

    if (id == GRN_ID_NIL)
	return Qnil;
    else
	return rb_grn_record_new(self, id);
}

static VALUE
rb_grn_table_key_support_array_reference (VALUE self, VALUE rb_id_or_key)
{
    if (RVAL2CBOOL(rb_obj_is_kind_of(rb_id_or_key, rb_cFixnum))) {
	int argc = 1;
	VALUE argv[1];
	argv[0] = rb_id_or_key;
	return rb_call_super(argc, argv);
    } else {
	return rb_grn_table_key_support_array_reference_by_key(self,
							       rb_id_or_key);
    }
}


static VALUE
rb_grn_table_key_support_array_set_by_key (VALUE self,
					   VALUE rb_key, VALUE rb_value)
{
    VALUE exception;
    grn_ctx *context;
    grn_obj *table;
    grn_id id;
    grn_obj *value;
    grn_rc rc;

    context = rb_grn_object_ensure_context(self, Qnil);
    table = SELF(self);

    if (NIL_P(rb_key))
	rb_raise(rb_eArgError, "key should not be nil: <%s>",
		 rb_grn_inspect(self));

    id = rb_grn_table_key_support_add_raw(self, rb_key, context, table);
    if (GRN_ID_NIL == id)
	rb_raise(rb_eGrnError,
		 "failed to add new record with key: <%s>: <%s>",
		 rb_grn_inspect(rb_key),
		 rb_grn_inspect(self));

    value = RVAL2GRNBULK(context, rb_value);
    rc = grn_obj_set_value(context, table, id, value, GRN_OBJ_SET);
    exception = rb_grn_context_to_exception(context, self);
    grn_obj_close(context, value);
    if (!NIL_P(exception))
	rb_exc_raise(exception);
    rb_grn_rc_check(rc, self);

    return rb_value;
}

static VALUE
rb_grn_table_key_support_array_set (VALUE self,
				    VALUE rb_id_or_key, VALUE rb_value)
{
    if (RVAL2CBOOL(rb_obj_is_kind_of(rb_id_or_key, rb_cFixnum))) {
	int argc = 2;
	VALUE argv[2];
	argv[0] = rb_id_or_key;
	argv[1] = rb_value;
	return rb_call_super(argc, argv);
    } else {
	return rb_grn_table_key_support_array_set_by_key(self,
							 rb_id_or_key,
							 rb_value);
    }
}

static VALUE
rb_grn_table_key_support_get_encoding (VALUE self)
{
    grn_ctx *context;
    grn_encoding encoding;
    grn_rc rc;

    context = rb_grn_object_ensure_context(self, Qnil);
    rc = grn_table_get_info(context, SELF(self), NULL, &encoding, NULL);
    rb_grn_context_check(context, self);
    rb_grn_rc_check(rc, self);

    return GRNENCODING2RVAL(encoding);
}

static VALUE
rb_grn_table_key_support_get_default_tokenizer (VALUE self)
{
    grn_ctx *context;
    grn_obj *tokenizer;
    grn_rc rc;

    context = rb_grn_object_ensure_context(self, Qnil);
    rc = grn_table_get_info(context, SELF(self),
			    NULL, NULL, &tokenizer);
    rb_grn_context_check(context, self);
    rb_grn_rc_check(rc, self);

    return GRNOBJECT2RVAL(Qnil, context, tokenizer);
}

static VALUE
rb_grn_table_key_support_set_default_tokenizer (VALUE self, VALUE rb_tokenizer)
{
    grn_ctx *context;
    grn_obj *tokenizer;
    grn_rc rc;

    context = rb_grn_object_ensure_context(self, Qnil);
    tokenizer = RVAL2GRNOBJECT(rb_tokenizer, context);
    rc = grn_obj_set_info(context, SELF(self),
			  GRN_INFO_DEFAULT_TOKENIZER, tokenizer);
    rb_grn_context_check(context, self);
    rb_grn_rc_check(rc, self);

    return Qnil;
}

void
rb_grn_init_table_key_support (VALUE mGrn)
{
    rb_mGrnTableKeySupport = rb_define_module_under(rb_cGrnTable, "KeySupport");

    rb_define_method(rb_mGrnTableKeySupport, "add",
		     rb_grn_table_key_support_add, 1);
    rb_define_method(rb_mGrnTableKeySupport, "key",
		     rb_grn_table_key_support_get_key, 1);

    rb_define_method(rb_mGrnTableKeySupport, "delete",
		     rb_grn_table_key_support_delete, 1);

    rb_define_method(rb_mGrnTableKeySupport, "[]",
		     rb_grn_table_key_support_array_reference, 1);
    rb_define_method(rb_mGrnTableKeySupport, "[]=",
		     rb_grn_table_key_support_array_set, 2);

    rb_define_method(rb_mGrnTableKeySupport, "encoding",
		     rb_grn_table_key_support_get_encoding, 0);

    rb_define_method(rb_mGrnTableKeySupport, "default_tokenizer",
		     rb_grn_table_key_support_get_default_tokenizer, 0);

    rb_define_method(rb_mGrnTableKeySupport, "default_tokenizer=",
		     rb_grn_table_key_support_set_default_tokenizer, 1);
}