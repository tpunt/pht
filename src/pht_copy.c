/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-present The PHP Group                             |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Thomas Punt <tpunt@php.net>                                  |
  +----------------------------------------------------------------------+
*/

#include "php_pht.h"
#include "src/pht_copy.h"
#include "src/pht_zend.h"
#include "src/classes/thread.h"

static void copy_executor_globals(void);
static zend_function *copy_function(zend_function *old_func, zend_class_entry *new_ce);
static zend_function *copy_internal_function(zend_function *old_func);
static zend_arg_info *copy_function_arg_info(zend_arg_info *old_arg_info, uint32_t fn_flags, uint32_t num_args);
static zend_op *copy_zend_op(zend_op_array *new_op_array, zend_op_array *old_op_array);
static zend_live_range *copy_zend_live_range(zend_live_range *old_live_range, uint32_t count);
static zend_try_catch_element *copy_zend_try_catch_element(zend_try_catch_element *old_try_catch, uint32_t count);
static HashTable *copy_static_variables(HashTable *old_static_variables);
static void copy_zend_op_array(zend_op_array *new_op_array, zend_op_array *old_op_array, zend_class_entry *new_ce);
static void copy_ini_directives(HashTable *new_ini_directives, HashTable *old_ini_directives);
static void copy_included_files(HashTable *new_included_files, HashTable *old_included_files);
static void copy_global_constants(HashTable *new_constants, HashTable *old_constants);
static void copy_class_constants(HashTable *new_constants_table, HashTable *old_constants_table, zend_class_entry *new_ce);
static void copy_class_constant(zend_class_constant *new_constant, zend_class_constant *old_constant, zend_class_entry *new_ce);
static void copy_constant(zend_constant *new_constant, zend_constant *old_constant);
static void copy_ces(HashTable *new_ces, HashTable *old_ces);
static zend_class_entry *copy_ce(zend_class_entry *old_ce);
static zend_class_entry *create_new_ce(zend_class_entry *old_ce);
static void copy_functions(HashTable *new_func_table, HashTable *old_func_table, zend_class_entry *new_ce);
static zend_trait_method_reference *copy_trait_method_reference(zend_trait_method_reference *method_reference);
static zend_trait_precedence **copy_trait_precedences(zend_trait_precedence **old_tps);
static zend_trait_alias **copy_trait_aliases(zend_trait_alias **old_tas);
static zend_class_entry **copy_traits(zend_class_entry **old_traits, uint32_t num_traits);
static zend_class_entry **copy_interfaces(zend_class_entry **old_interfaces, uint32_t num_interfaces);
static zval *copy_zval_table(zval *old_table, int count);
static zend_function *set_function_from_name(HashTable function_table, char *name, size_t length);
static void copy_iterator_functions(zend_class_iterator_funcs *new_if, zend_class_iterator_funcs old_if);
static void copy_doc_comment(zend_string **new_doc_comment, zend_string *old_doc_comment);
static void copy_properties_info(HashTable *new_properties_info, HashTable *old_properties_info, zend_class_entry *new_ce);

void copy_execution_context(void)
{
    copy_executor_globals();
}

static void copy_executor_globals(void)
{
    // unitialized_zval
    // error_zval

    // symtable_cache
    // symtable_cache_limit
    // symtable_cache_ptr

    // symbol_table
    copy_included_files(&EG(included_files), &PHT_EG(PHT_ZG(parent_thread_ls), included_files));

    // bailout

    // error_reporting
    // exit_status

    copy_functions(EG(function_table), PHT_EG(PHT_ZG(parent_thread_ls), function_table), NULL);
    copy_ces(EG(class_table), PHT_CG(PHT_ZG(parent_thread_ls), class_table));
    copy_global_constants(EG(zend_constants), PHT_EG(PHT_ZG(parent_thread_ls), zend_constants));

    // vm_stack_top
    // vm_stack_end
    // vm_stack

    // current_execute_data
    // fake_scope

    // precision

    // ticks_count

    // in_autoload
    // autoload_func
    // full_tables_cleanup

    // no_extensions

    // vm_interrupt
    // timed_out
    // hard_timeout

#ifdef ZEND_WIN32
    // windows_version_info
#endif

    // regular_list
    // persistent_list

    // user_error_handler_error_reporting
    // user_error_handler
    // user_exception_handler
    // user_error_handlers_error_reporting
    // user_error_handlers
    // user_exception_handlers

    // error_handling
    // exception_class

    // timeout_seconds

    // lambda_count

    copy_ini_directives(EG(ini_directives), PHT_EG(PHT_ZG(parent_thread_ls), ini_directives));
    // modified_ini_directives
    // error_reporting_ini_entry

    // objects_store
    // exception
    // prev_exception
    // opline_before_exception
    // exception_op

    // current_module

    // active
    // valid_symbol_table

    // assertions

    // ht_iterators_count
    // ht_iterators_used
    // ht_iterators
    // ht_iterators_slots

    // saved_fpu_cw_ptr
#if XPFPA_HAVE_CW
    // saved_fpu_cw;
#endif

    // trampoline
    // call_trampoline_op

    // reserved
}

static zend_function *copy_function(zend_function *old_func, zend_class_entry *new_ce)
{
    zend_function *new_func;

    // check if function exists? If it's a method, parent class function_tables may also need to be checked

    if (old_func->type == ZEND_INTERNAL_FUNCTION) {
        new_func = copy_internal_function(old_func);
    } else {
        new_func = copy_user_function(old_func, new_ce);
    }

    return new_func;
}

static zend_arg_info *copy_function_arg_info(zend_arg_info *old_arg_info, uint32_t fn_flags, uint32_t num_args)
{
    if (fn_flags & ZEND_ACC_HAS_RETURN_TYPE) {
        --old_arg_info;
        ++num_args;
    }

    if (fn_flags & ZEND_ACC_VARIADIC) {
        ++num_args;
    }

    zend_arg_info *new_arg_info = emalloc(sizeof(zend_arg_info) * num_args);

    memcpy(new_arg_info, old_arg_info, sizeof(zend_arg_info) * num_args);

    for (int i = 0; i < num_args; ++i) {
        if (old_arg_info[i].name) {
            new_arg_info[i].name = zend_string_dup(old_arg_info[i].name, 0);
        }

        if (ZEND_TYPE_IS_CLASS(old_arg_info[i].type)) {
            zend_string *type_name = zend_string_dup(ZEND_TYPE_NAME(old_arg_info[i].type), 0);
            zend_long allow_null = ZEND_TYPE_ALLOW_NULL(old_arg_info[i].type);

            new_arg_info[i].type = ZEND_TYPE_ENCODE_CLASS(type_name, allow_null);
        }
    }

    if (fn_flags & ZEND_ACC_HAS_RETURN_TYPE) {
        ++new_arg_info;
    }

    return new_arg_info;
}

static zend_op *copy_zend_op(zend_op_array *new_op_array, zend_op_array *old_op_array)
{
    zend_op *new_opcodes = emalloc(sizeof(zend_op) * old_op_array->last);

    memcpy(new_opcodes, old_op_array->opcodes, sizeof(zend_op) * old_op_array->last);

    return new_opcodes;
}

static zend_live_range *copy_zend_live_range(zend_live_range *old_live_range, uint32_t count)
{
    if (!old_live_range) {
        return NULL;
    }

    zend_live_range *new_live_range = emalloc(sizeof(zend_live_range) * count);

    memcpy(new_live_range, old_live_range, sizeof(zend_live_range) * count);

    return new_live_range;
}

static zend_try_catch_element *copy_zend_try_catch_element(zend_try_catch_element *old_try_catch, uint32_t count)
{
    if (!old_try_catch) {
        return NULL;
    }

    zend_try_catch_element *new_try_catch = emalloc(sizeof(zend_try_catch_element) * count);

    memcpy(new_try_catch, old_try_catch, sizeof(zend_try_catch_element) * count);

    return new_try_catch;
}

static HashTable *copy_static_variables(HashTable *old_static_variables)
{
    if (!old_static_variables) {
        return NULL;
    }

    HashTable *new_static_variables;
    zend_string *key;
    zval *value;

    ALLOC_HASHTABLE(new_static_variables);
    zend_hash_init(new_static_variables, zend_hash_num_elements(old_static_variables), NULL, ZVAL_PTR_DTOR, 0);

    ZEND_HASH_FOREACH_STR_KEY_VAL(old_static_variables, key, value) {
        zend_string *new_key = zend_string_dup(key, 0);

        while (Z_ISREF_P(value)) {
            value = Z_REFVAL_P(value);
        }

        if (!Z_REFCOUNTED_P(value)) {
            zend_hash_add(new_static_variables, new_key, value);
        } else {
            zval copy;

            switch (Z_TYPE_P(value)) {
                case IS_STRING:
                    ZVAL_NEW_STR(&copy, zend_string_dup(Z_STR_P(value), 0));
                    break;
                case IS_ARRAY:
                    ZVAL_ARR(&copy, pht_zend_array_dup(Z_ARR_P(value)));
                    break;
                case IS_CONSTANT:
                    // constant names are not interned (in PHP 7.2, at least)
                    ZVAL_NEW_STR(&copy, zend_string_dup(Z_STR_P(value), 0));
                    break;
                case IS_CONSTANT_AST: // @todo PHP 7.2 specific
                    ZVAL_NEW_AST(&copy, pht_zend_ast_copy(Z_ASTVAL_P(value)));
                    break;
                case IS_OBJECT:
                    ZVAL_NULL(&copy);
                    break;
                default:
                    ZEND_ASSERT(0);
            }

            zend_hash_add(new_static_variables, new_key, &copy);
        }

        zend_string_release(new_key);
    } ZEND_HASH_FOREACH_END();

    return new_static_variables;
}

static void copy_zend_op_array(zend_op_array *new_op_array, zend_op_array *old_op_array, zend_class_entry *new_ce)
{
    new_op_array->type = old_op_array->type;
    memcpy(new_op_array->arg_flags, old_op_array->arg_flags, sizeof(zend_uchar) * 3);
    new_op_array->fn_flags = old_op_array->fn_flags;
    new_op_array->function_name = zend_string_dup(old_op_array->function_name, 0);
    new_op_array->scope = new_ce;
    new_op_array->prototype = NULL;
    new_op_array->num_args = old_op_array->num_args;
    new_op_array->required_num_args = old_op_array->required_num_args;
    new_op_array->arg_info = copy_function_arg_info(old_op_array->arg_info, old_op_array->fn_flags, old_op_array->num_args);
    new_op_array->refcount = emalloc(sizeof(uint32_t));
    *new_op_array->refcount = 1;
    new_op_array->last = old_op_array->last;

    new_op_array->last_var = old_op_array->last_var;
    new_op_array->T = old_op_array->T;
    new_op_array->vars = emalloc(sizeof(zend_string *) * old_op_array->last_var);
    memcpy(new_op_array->vars, old_op_array->vars, sizeof(zend_string *) * old_op_array->last_var);
    new_op_array->last_live_range = old_op_array->last_live_range;
    new_op_array->last_try_catch = old_op_array->last_try_catch;
    new_op_array->live_range = copy_zend_live_range(old_op_array->live_range, old_op_array->last_live_range);
    new_op_array->try_catch_array = copy_zend_try_catch_element(old_op_array->try_catch_array, old_op_array->last_try_catch);
    new_op_array->static_variables = copy_static_variables(old_op_array->static_variables);

    if (!(new_op_array->filename = zend_hash_find_ptr(&PHT_ZG(op_array_file_names), old_op_array->filename))) {
        zend_string *filename = zend_string_dup(old_op_array->filename, 0);

        new_op_array->filename = zend_hash_add_ptr(&PHT_ZG(op_array_file_names), filename, filename);
        zend_string_release(filename);
    }

    new_op_array->line_start = old_op_array->line_start;
    new_op_array->line_end = old_op_array->line_end;
    new_op_array->doc_comment = old_op_array->doc_comment ? zend_string_dup(old_op_array->doc_comment, 0) : NULL;
    new_op_array->early_binding = old_op_array->early_binding;
    new_op_array->last_literal = old_op_array->last_literal;
    new_op_array->literals = emalloc(sizeof(zval) * old_op_array->last_literal);
    memcpy(new_op_array->literals, old_op_array->literals, sizeof(zval) * old_op_array->last_literal);

    for (int i = 0; i < old_op_array->last_literal; ++i) {
        switch (Z_TYPE(old_op_array->literals[i])) {
            case IS_ARRAY:
                ZVAL_ARR(new_op_array->literals + i, pht_zend_array_dup(Z_ARR(old_op_array->literals[i])));
                break;
#if PHP_VERSION_ID < 70300
            case IS_CONSTANT: // constant names are interned, so nothing to do
                break;
#endif
            case IS_CONSTANT_AST:
                ZVAL_NEW_AST(new_op_array->literals + i, pht_zend_ast_copy(Z_ASTVAL(new_op_array->literals[i])));
        }
    }

    new_op_array->opcodes = copy_zend_op(new_op_array, old_op_array);

    new_op_array->cache_size = old_op_array->cache_size;
    new_op_array->run_time_cache = NULL;
    memcpy(new_op_array->reserved, old_op_array->reserved, sizeof(void *) * ZEND_MAX_RESERVED_RESOURCES);
}

zend_function *copy_user_function(zend_function *old_func, zend_class_entry *new_ce)
{
    zend_function *new_func = zend_arena_alloc(&CG(arena), sizeof(zend_function));

    copy_zend_op_array(&new_func->op_array, &old_func->op_array, new_ce);

    return new_func;
}

static zend_function *copy_internal_function(zend_function *old_func)
{
    zend_function *new_func = emalloc(sizeof(zend_internal_function));

    memcpy(new_func, old_func, sizeof(zend_internal_function));

    return new_func;
}

static void copy_ini_directives(HashTable *new_ini_directives, HashTable *old_ini_directives)
{
    zend_ini_entry *old_ini_entry;
    zend_string *ini_name;

    ZEND_HASH_FOREACH_STR_KEY_PTR(old_ini_directives, ini_name, old_ini_entry) {
        zend_ini_entry *new_ini_entry = zend_hash_find_ptr(new_ini_directives, ini_name);

        if (!new_ini_entry || !old_ini_entry->value || !new_ini_entry->value) {
            continue;
        }

        if (strcmp(ZSTR_VAL(old_ini_entry->value), ZSTR_VAL(new_ini_entry->value)) == SUCCESS) {
            continue;
        }

        zend_bool modifiable = new_ini_entry->modifiable;
        zend_string *new_ini_name = zend_string_dup(ini_name, 0);

        if (!EG(modified_ini_directives)) {
            ALLOC_HASHTABLE(EG(modified_ini_directives));
            zend_hash_init(EG(modified_ini_directives), 8, NULL, NULL, 0);
        }

        if (!new_ini_entry->modified) {
            new_ini_entry->orig_value = new_ini_entry->value;
            new_ini_entry->orig_modifiable = new_ini_entry->modifiable;
            new_ini_entry->modified = 1;
            zend_hash_add_ptr(EG(modified_ini_directives), new_ini_name, new_ini_entry);
        }

        new_ini_entry->modifiable = 1;
        new_ini_entry->on_modify(new_ini_entry, old_ini_entry->value, new_ini_entry->mh_arg1, new_ini_entry->mh_arg2, new_ini_entry->mh_arg3, ZEND_INI_SYSTEM);

        if (new_ini_entry->modified && new_ini_entry->orig_value != new_ini_entry->value) {
            zend_string_release(new_ini_entry->value);
        }

        new_ini_entry->value = zend_string_dup(old_ini_entry->value, 0);
        new_ini_entry->modifiable = modifiable;

        zend_string_release(new_ini_name);
    } ZEND_HASH_FOREACH_END();
}

static void copy_included_files(HashTable *new_included_files, HashTable *old_included_files)
{
    zend_string *filename;

    ZEND_HASH_FOREACH_STR_KEY(old_included_files, filename) {
        zend_string *new_filename = zend_string_dup(filename, 0);

        zend_hash_add_empty_element(new_included_files, new_filename);

        zend_string_release(new_filename);
    } ZEND_HASH_FOREACH_END();
}

static void copy_global_constants(HashTable *new_constants, HashTable *old_constants)
{
    zend_constant *old_constant;
    zend_string *name;

    ZEND_HASH_FOREACH_STR_KEY_PTR(old_constants, name, old_constant) {
        zend_constant new_constant;

        // @todo constant && !STDIN, !STDOUT, !STDERR ?

        if (zend_hash_exists(new_constants, name)) {
            continue;
        }

        zend_string *lowercased = zend_string_tolower(name);

        if (zend_hash_exists(new_constants, lowercased)) {
            zend_string_free(lowercased);
            continue;
        }

        zend_string_free(lowercased);

        copy_constant(&new_constant, old_constant);

        zend_register_constant(&new_constant);
    } ZEND_HASH_FOREACH_END();
}

static void copy_class_constants(HashTable *new_constants_table, HashTable *old_constants_table, zend_class_entry *new_ce)
{
    zend_string *key;
    zend_class_constant *old_constant, new_constant;

    ZEND_HASH_FOREACH_STR_KEY_PTR(old_constants_table, key, old_constant) {
        copy_class_constant(&new_constant, old_constant, new_ce);
        zend_hash_add_mem(new_constants_table, key, &new_constant, sizeof(zend_class_constant));
    } ZEND_HASH_FOREACH_END();
}

static void copy_class_constant(zend_class_constant *new_constant, zend_class_constant *old_constant, zend_class_entry *new_ce)
{
    PHT_ZVAL_DUP(&new_constant->value, &old_constant->value);
    new_constant->value.u2 = old_constant->value.u2;

    if (old_constant->doc_comment) {
        new_constant->doc_comment = zend_string_dup(old_constant->doc_comment, 0); // not interned
    } else {
        new_constant->doc_comment = NULL;
    }

    new_constant->ce = new_ce;
}

static void copy_constant(zend_constant *new_constant, zend_constant *old_constant)
{
    PHT_ZVAL_DUP(&new_constant->value, &old_constant->value);

    new_constant->name = old_constant->name;
    new_constant->flags = old_constant->flags;
    new_constant->module_number = old_constant->module_number;
}

static void copy_ces(HashTable *new_ces, HashTable *old_ces)
{
    zend_class_entry *old_ce;

    ZEND_HASH_FOREACH_PTR(old_ces, old_ce) {
        if (old_ce->type == ZEND_USER_CLASS) {
            zend_class_entry *new_ce = copy_ce(old_ce);

            if (new_ce) { // skip past anonymous classes
                zend_string *new_ce_name = zend_string_tolower(old_ce->name); // ce names are interned

                zend_hash_add_ptr(new_ces, new_ce_name, new_ce);

                zend_string_release(new_ce_name);
            }
        }
    } ZEND_HASH_FOREACH_END();
}

static zend_class_entry *copy_ce(zend_class_entry *old_ce)
{
    if (old_ce == NULL) {
        return NULL;
    }

    if (old_ce->type == ZEND_INTERNAL_CLASS) {
        return zend_lookup_class(old_ce->name);
    }

    if (old_ce->ce_flags & ZEND_ACC_ANON_CLASS) {
        return NULL;
    }

    zend_string *new_ce_name = zend_string_tolower(old_ce->name);
    zend_class_entry *new_ce = zend_hash_find_ptr(EG(class_table), new_ce_name);

    if (!new_ce) {
        new_ce = create_new_ce(old_ce);
    }

    zend_string_release(new_ce_name);

    return new_ce;
}

static zend_class_entry *create_new_ce(zend_class_entry *old_ce)
{
    zend_class_entry *new_ce = zend_arena_calloc(&CG(arena), 1, sizeof(zend_class_entry));

    zend_initialize_class_data(new_ce, 1);

    // just perform a memcpy instead and then reassign stuff as necessary?
    // 16/43 members are direct copies (27/43 require manual copying)

    new_ce->type = old_ce->type;
    new_ce->name = zend_string_dup(old_ce->name, 0);
    new_ce->parent = copy_ce(old_ce->parent);
    new_ce->refcount = 1;
    new_ce->ce_flags = old_ce->ce_flags;

    new_ce->default_properties_count = old_ce->default_properties_count;
    new_ce->default_static_members_count = old_ce->default_static_members_count;
    new_ce->default_properties_table = copy_zval_table(old_ce->default_properties_table, old_ce->default_properties_count);
    new_ce->default_static_members_table = copy_zval_table(old_ce->default_static_members_table, old_ce->default_static_members_count);
    new_ce->static_members_table = copy_zval_table(old_ce->default_static_members_table, old_ce->default_static_members_count);

    copy_functions(&new_ce->function_table, &old_ce->function_table, new_ce);
    copy_properties_info(&new_ce->properties_info, &old_ce->properties_info, new_ce);
    copy_class_constants(&new_ce->constants_table, &old_ce->constants_table, new_ce);

    new_ce->constructor = set_function_from_name(new_ce->function_table, ZEND_STRL("__construct")); // @todo may also be class name
    new_ce->destructor = set_function_from_name(new_ce->function_table, ZEND_STRL("__destruct"));
    new_ce->clone = set_function_from_name(new_ce->function_table, ZEND_STRL("__clone"));
    new_ce->__get = set_function_from_name(new_ce->function_table, ZEND_STRL("__get"));
    new_ce->__set = set_function_from_name(new_ce->function_table, ZEND_STRL("__set"));
    new_ce->__unset = set_function_from_name(new_ce->function_table, ZEND_STRL("__unset"));
    new_ce->__isset = set_function_from_name(new_ce->function_table, ZEND_STRL("__isset"));
    new_ce->__call = set_function_from_name(new_ce->function_table, ZEND_STRL("__call"));
    new_ce->__callstatic = set_function_from_name(new_ce->function_table, ZEND_STRL("__callstatic"));
    new_ce->__tostring = set_function_from_name(new_ce->function_table, ZEND_STRL("__tostring"));
    new_ce->__debugInfo = set_function_from_name(new_ce->function_table, ZEND_STRL("__debuginfo"));
    new_ce->serialize_func = set_function_from_name(new_ce->function_table, ZEND_STRL("serialize"));
    new_ce->unserialize_func = set_function_from_name(new_ce->function_table, ZEND_STRL("unserialize"));

    copy_iterator_functions(&new_ce->iterator_funcs, old_ce->iterator_funcs);

    new_ce->create_object = old_ce->create_object;
    new_ce->get_iterator = old_ce->get_iterator;
    new_ce->interface_gets_implemented = old_ce->interface_gets_implemented;
    new_ce->get_static_method = old_ce->get_static_method;

    new_ce->serialize = old_ce->serialize;
    new_ce->unserialize = old_ce->unserialize;

    new_ce->num_interfaces = old_ce->num_interfaces;
    new_ce->num_traits = old_ce->num_traits;
    new_ce->interfaces = copy_interfaces(old_ce->interfaces, old_ce->num_interfaces);

    new_ce->traits = copy_traits(old_ce->traits, old_ce->num_traits);
    new_ce->trait_aliases = copy_trait_aliases(old_ce->trait_aliases);
    new_ce->trait_precedences = copy_trait_precedences(old_ce->trait_precedences);

    new_ce->info.user.filename = old_ce->info.user.filename;
    new_ce->info.user.line_start = old_ce->info.user.line_start;
    new_ce->info.user.line_end = old_ce->info.user.line_end;
    copy_doc_comment(&new_ce->info.user.doc_comment, old_ce->info.user.doc_comment);

    return new_ce;
}

static void copy_functions(HashTable *new_func_table, HashTable *old_func_table, zend_class_entry *new_ce)
{
    zend_string *old_func_name;
    zend_function *old_func;

    ZEND_HASH_FOREACH_STR_KEY_PTR(old_func_table, old_func_name, old_func) {
        if (new_ce) { // new_ce || old_func-type == ZEND_USER_FUNCTION ?
            zend_string *new_func_name = zend_string_dup(old_func_name, 0);
            zend_function *new_func = copy_function(old_func, new_ce);

            zend_hash_add_ptr(new_func_table, new_func_name, new_func);
            zend_string_release(new_func_name);
        } else {
            // internal functions are already copied on php request startup into EG(function_table)
            if (old_func->type == ZEND_USER_FUNCTION) {
                zend_string *new_func_name = zend_string_dup(old_func_name, 0);
                zend_function *new_func = copy_function(old_func, new_ce);

                if (new_func) {
                    if (!zend_hash_add_ptr(new_func_table, new_func_name, new_func)) {
                        destroy_op_array((zend_op_array *) new_func);
                    }
                }

                zend_string_release(new_func_name);
            }
        }
    } ZEND_HASH_FOREACH_END();
}

static void copy_properties_info(HashTable *new_properties_info, HashTable *old_properties_info, zend_class_entry *new_ce)
{
    zend_string *key;
    zend_property_info *value;

    ZEND_HASH_FOREACH_STR_KEY_PTR(old_properties_info, key, value) {
        zend_property_info prop_info = *value;

        if (prop_info.doc_comment) {
            prop_info.doc_comment = zend_string_dup(prop_info.doc_comment, 0);
        }

        prop_info.ce = new_ce; // @todo what about parent properties?

        // @todo check for success
        zend_hash_add_mem(new_properties_info, key, &prop_info, sizeof(zend_property_info));
    } ZEND_HASH_FOREACH_END();
}

static void copy_doc_comment(zend_string **new_doc_comment, zend_string *old_doc_comment)
{
    if (old_doc_comment) {
        *new_doc_comment = zend_string_dup(old_doc_comment, 0);
    }
}

static zend_trait_method_reference *copy_trait_method_reference(zend_trait_method_reference *method_reference)
{
    zend_trait_method_reference *new_method_reference = emalloc(sizeof(zend_trait_method_reference));

    new_method_reference->method_name = zend_string_dup(method_reference->method_name, 0);
    new_method_reference->ce = copy_ce(method_reference->ce);

    if (method_reference->class_name) {
        new_method_reference->class_name = zend_string_dup(method_reference->class_name, 0);
    } else {
        new_method_reference->class_name = NULL;
    }

    return new_method_reference;
}

static zend_trait_precedence **copy_trait_precedences(zend_trait_precedence **old_tps)
{
    if (old_tps == NULL) {
        return NULL;
    }

    uint32_t count = 0;

    while (old_tps[count++]);

    zend_trait_precedence **new_tps = emalloc(sizeof(zend_trait_precedence *) * count);

    new_tps[--count] = NULL;

    while (count--) {
        new_tps[count] = emalloc(sizeof(zend_trait_precedence));
        new_tps[count]->trait_method = copy_trait_method_reference(old_tps[count]->trait_method);

        if (old_tps[count]->exclude_from_classes) {
            new_tps[count]->exclude_from_classes = malloc(sizeof(*old_tps[count]->exclude_from_classes));
            new_tps[count]->exclude_from_classes->ce = copy_ce(old_tps[count]->exclude_from_classes->ce);
            new_tps[count]->exclude_from_classes->class_name = zend_string_dup(old_tps[count]->exclude_from_classes->class_name, 0);
        } else {
            new_tps[count]->exclude_from_classes = NULL;
        }
    }

    return new_tps;
}

static zend_trait_alias **copy_trait_aliases(zend_trait_alias **old_tas)
{
    if (old_tas == NULL) {
        return NULL;
    }

    uint32_t count = 0;

    while (old_tas[count++]);

    zend_trait_alias **new_tas = emalloc(sizeof(zend_trait_alias *) * count);

    new_tas[--count] = NULL;

    while (count--) {
        new_tas[count] = emalloc(sizeof(zend_trait_alias));
        new_tas[count]->trait_method = copy_trait_method_reference(old_tas[count]->trait_method);
        new_tas[count]->alias = old_tas[count]->alias ? zend_string_dup(old_tas[count]->alias, 0) : NULL;
        new_tas[count]->modifiers = old_tas[count]->modifiers;
    }

    return new_tas;
}

static zend_class_entry **copy_traits(zend_class_entry **old_traits, uint32_t num_traits)
{
    if (old_traits == NULL) {
        return NULL;
    }

    zend_class_entry **new_traits = emalloc(sizeof(zend_class_entry *) * num_traits);

    for (uint32_t i = 0; i < num_traits; ++i) {
        new_traits[i] = copy_ce(old_traits[i]);
    }

    return new_traits;
}

static zend_class_entry **copy_interfaces(zend_class_entry **old_interfaces, uint32_t num_interfaces)
{
    if (old_interfaces == NULL) {
        return NULL;
    }

    zend_class_entry **new_interfaces = emalloc(sizeof(zend_class_entry *) * num_interfaces);

    for (uint32_t i = 0; i < num_interfaces; ++i) {
        new_interfaces[i] = copy_ce(old_interfaces[i]);
    }

    return new_interfaces;
}

static zval *copy_zval_table(zval *old_table, int count)
{
    if (old_table == NULL) {
        return NULL;
    }

    zval *new_table = emalloc(sizeof(zval) * count);

    memcpy(new_table, old_table, sizeof(zval) * count);

    for (int i = 0; i < count; ++i) {
        if (Z_REFCOUNTED(old_table[i])) {
            switch (Z_TYPE(old_table[i])) {
                case IS_STRING:
                    ZVAL_NEW_STR(new_table + i, zend_string_dup(Z_STR(old_table[i]), 0));
                    break;
                case IS_ARRAY:
                    ZVAL_ARR(new_table + i, pht_zend_array_dup(Z_ARR(old_table[i])));
                    break;
                case IS_OBJECT: // @todo a temporary solution
                    ZVAL_NULL(new_table + i);
                    break;
                default:
                    ZEND_ASSERT(0);
            }
        }
    }

    return new_table;
}

static zend_function *set_function_from_name(HashTable function_table, char *name, size_t length)
{
    return zend_hash_str_find_ptr(&function_table, name, length);
}

static void copy_iterator_functions(zend_class_iterator_funcs *new_if, zend_class_iterator_funcs old_if)
{
    *new_if = old_if;

    // new_if->zf_new_iterator = zend_hash_index_find_ptr(&PHT_ZG(symbol_tracker), (zend_ulong) old_if.zf_new_iterator);
    // new_if->zf_valid = zend_hash_index_find_ptr(&PHT_ZG(symbol_tracker), (zend_ulong) old_if.zf_valid);
    // new_if->zf_current = zend_hash_index_find_ptr(&PHT_ZG(symbol_tracker), (zend_ulong) old_if.zf_current);
    // new_if->zf_key = zend_hash_index_find_ptr(&PHT_ZG(symbol_tracker), (zend_ulong) old_if.zf_key);
    // new_if->zf_next = zend_hash_index_find_ptr(&PHT_ZG(symbol_tracker), (zend_ulong) old_if.zf_next);
    // new_if->zf_rewind = zend_hash_index_find_ptr(&PHT_ZG(symbol_tracker), (zend_ulong) old_if.zf_rewind);
}
