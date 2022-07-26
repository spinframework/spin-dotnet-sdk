#include <stdlib.h>
#include <string.h>

#include "./spin-http.h"

#include <mono-wasi/driver.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/class.h>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/image.h>
#include <mono/metadata/metadata.h>
#include <mono/metadata/object.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/reflection.h>
#include <mono/utils/mono-publib.h>

#include "./util.h"

// These are generated by the WASI SDK during build
const char* dotnet_wasi_getentrypointassemblyname();
const char* dotnet_wasi_getbundledfile(const char* name, int* out_length);
void dotnet_wasi_registerbundledassemblies();

typedef uint8_t conversion_err_t;

#define CONV_ERR_OK 0
#define CONV_ERR_CONVERSION_NOT_FOUND 1
#define CONV_ERR_CONVERSION_EXCEPTION 2
#define CONV_ERR_SETTER_ERR 3
#define CONV_ERR_GETTER_ERR 4
#define CONV_ERR_NULL 5

// Utility converters

MonoArray* string_pair_array(MonoClass* pair_class, spin_http_tuple2_string_string_t *values, size_t len) {
    MonoArray* array = mono_array_new(mono_domain_get(), pair_class, len);
    for (size_t index = 0; index < len; ++index) {
        MonoObject* pair = mono_object_new(mono_domain_get(), pair_class);
        mono_runtime_object_init(pair);
        set_field(pair_class, pair, "Key", mono_string_new_len(mono_domain_get(), values[index].f0.ptr, values[index].f0.len));
        set_field(pair_class, pair, "Value", mono_string_new_len(mono_domain_get(), values[index].f1.ptr, values[index].f1.len));
        mono_array_setref(array, index, pair);
    }
    return array;
}

MonoArray* body_array(spin_http_option_body_t* body) {
    MonoClass* byte_class = mono_get_byte_class();
    if (body->is_some) {
        spin_http_body_t* val = &body->val;
        MonoArray* array = mono_array_new(mono_domain_get(), byte_class, val->len);
        mono_value_copy_array(array, 0, val->ptr, val->len);
        return array;    
    } else {
        return mono_array_new(mono_domain_get(), byte_class, 0);
    }
}

// Main application object conversion

conversion_err_t wit_request_to_interop_request(MonoImage* sdk_image, MonoClass* builder_class, spin_http_request_t *req, MonoObject** interop_request) {
    MonoObject* builder = mono_object_new(mono_domain_get(), builder_class);
    if (!builder) {
        return CONV_ERR_CONVERSION_NOT_FOUND;
    }
    mono_runtime_object_init(builder);

    MonoClass* pair_class = mono_class_from_name(sdk_image, "Fermyon.Spin.Sdk", "StringPair");
    if (!pair_class) {
        return CONV_ERR_CONVERSION_NOT_FOUND;
    }

    set_member_err_t set_err;

    set_err = set_field(builder_class, builder, "Method", &(req->method));
    if (set_err) {
        return CONV_ERR_SETTER_ERR;
    }

    MonoString* uri_value = mono_string_new(mono_domain_get(), req->uri.ptr);
    set_err = set_field(builder_class, builder, "Uri", uri_value);
    if (set_err) {
        return CONV_ERR_SETTER_ERR;
    }

    MonoArray* header_value = string_pair_array(pair_class, req->headers.ptr, req->headers.len);
    set_err = set_field(builder_class, builder, "Headers", header_value);
    if (set_err) {
        return CONV_ERR_SETTER_ERR;
    }

    MonoArray* params_value = string_pair_array(pair_class, req->params.ptr, req->params.len);
    set_err = set_field(builder_class, builder, "Parameters", params_value);
    if (set_err) {
        return CONV_ERR_SETTER_ERR;
    }

    MonoArray* body_value = body_array(&req->body);
    set_err = set_field(builder_class, builder, "Body", body_value);
    if (set_err) {
        return CONV_ERR_SETTER_ERR;
    }

    *interop_request = builder;
    return CONV_ERR_OK;
}

conversion_err_t interop_request_to_sdk_request(MonoClass* builder_class, MonoObject* builder, MonoObject** request_obj) {
    MonoMethod* build_method = mono_class_get_method_from_name(builder_class, "Build", 0);
    if (!build_method) {
        return CONV_ERR_CONVERSION_NOT_FOUND;
    }

    MonoObject* exn;
    MonoObject* request_instance = mono_wasm_invoke_method(build_method, builder, NULL, &exn);
    if (exn) {
        return CONV_ERR_CONVERSION_EXCEPTION;
    }
    if (!request_instance) {
        return CONV_ERR_NULL;
    }
    *request_obj = request_instance;
    return CONV_ERR_OK;
}

conversion_err_t sdk_response_to_interop_response(MonoObject* resp, MonoObject** response_interop) {
    MonoClass* resp_class = mono_object_get_class(resp);
    if (!resp_class) {
        return CONV_ERR_CONVERSION_NOT_FOUND;
    }
 
    MonoMethod* to_interop = mono_class_get_method_from_name(resp_class, "ToInterop", 0);
    if (!to_interop) {
        return CONV_ERR_CONVERSION_NOT_FOUND;
    }

    MonoObject* exn;
    MonoObject* resp_interop = mono_wasm_invoke_method(to_interop, resp, NULL, &exn);
    if (exn) {
        return CONV_ERR_CONVERSION_EXCEPTION;
    }
    if (!resp_interop) {
        return CONV_ERR_NULL;
    }

    *response_interop = resp_interop;
    return CONV_ERR_OK;
}

conversion_err_t interop_headers_to_wit_headers(MonoArray* headers, spin_http_option_headers_t* result) {
    spin_http_option_headers_t wit_headers;

    uintptr_t headers_len = mono_array_length(headers);
    
    if (headers_len == 0) {
        wit_headers.is_some = false;
        *result = wit_headers;
        return CONV_ERR_OK;
    }

    MonoClass* pair_class = mono_object_get_class(mono_array_get(headers, MonoObject*, 0));
    if (!pair_class) {
        return CONV_ERR_CONVERSION_NOT_FOUND;
    }

    spin_http_tuple2_string_string_t* header_tuples = (spin_http_tuple2_string_string_t*)malloc(headers_len * sizeof(spin_http_tuple2_string_string_t));
    for (int index = 0; index < headers_len; ++index) {
        MonoObject* string_pair = mono_array_get(headers, MonoObject*, index);
        if (!string_pair) {
            return CONV_ERR_CONVERSION_NOT_FOUND;
        }

        MonoString* key_obj;
        MonoString* value_obj;
        if (get_field(pair_class, string_pair, "Key", &key_obj) != GET_MEMBER_ERR_OK) {
            return CONV_ERR_GETTER_ERR;
        }
        if (get_field(pair_class, string_pair, "Value", &value_obj) != GET_MEMBER_ERR_OK) {
            return CONV_ERR_GETTER_ERR;
        }

        char* key_c_str = mono_wasm_string_get_utf8(key_obj);
        char* value_c_str = mono_wasm_string_get_utf8(value_obj);
        spin_http_string_t key = { key_c_str, strlen(key_c_str) };
        spin_http_string_t value = { value_c_str, strlen(value_c_str) };
        header_tuples[index].f0 = key;
        header_tuples[index].f1 = value;
    }
    wit_headers.is_some = true;
    wit_headers.val.ptr = header_tuples;
    wit_headers.val.len = headers_len;

    *result = wit_headers;
    return CONV_ERR_OK;
}

spin_http_option_body_t interop_body_to_wit_body(MonoArray* body) {
    spin_http_option_body_t wit_body;

    uintptr_t body_len = mono_array_length(body);

    if (body_len == 0) {
        wit_body.is_some = false;
        return wit_body;
    }

    uint8_t* body_bytes = (uint8_t*)malloc(body_len * sizeof(uint8_t));
    for (int index = 0; index < body_len; ++index) {
        body_bytes[index] = mono_array_get(body, uint8_t, index);
    }

    wit_body.is_some = true;
    wit_body.val.ptr = body_bytes;
    wit_body.val.len = body_len;
    return wit_body;
}

conversion_err_t interop_response_to_wit_response(MonoObject* response_obj, spin_http_response_t* result) {
    MonoClass* response_class = mono_object_get_class(response_obj);
    if (!response_class) {
        return CONV_ERR_CONVERSION_NOT_FOUND;
    }

    uint16_t status;
    if (get_field(response_class, response_obj, "Status", &status) != GET_MEMBER_ERR_OK) {
        return CONV_ERR_GETTER_ERR;
    }

    MonoArray* headers;
    if (get_field(response_class, response_obj, "Headers", &headers) != GET_MEMBER_ERR_OK) {
        return CONV_ERR_GETTER_ERR;
    }
    spin_http_option_headers_t wit_headers;
    conversion_err_t h_err = interop_headers_to_wit_headers(headers, &wit_headers);
    if (h_err) {
        return h_err;
    }

    MonoArray* body;
    if (get_field(response_class, response_obj, "Body", &body) != GET_MEMBER_ERR_OK) {
        return CONV_ERR_GETTER_ERR;
    }
    spin_http_option_body_t wit_body = interop_body_to_wit_body(body);

    spin_http_response_t wit_response = {
        status,
        wit_headers,
        wit_body
    };

    *result = wit_response;
    return CONV_ERR_OK;
}

void call_clr_request_handler(MonoMethod* handler, MonoObject* request_obj, MonoObject** response_obj, MonoObject** exn) {
    *exn = NULL;

    void *params[1];
    params[0] = request_obj;
    *response_obj = mono_wasm_invoke_method(handler, NULL, params, exn);
}

spin_http_response_t internal_error(const char* message) {
    spin_http_response_t response;
    response.status = 500;
    response.headers.is_some = false;
    response.body.is_some = true;
    response.body.val.ptr = (uint8_t*)message;
    response.body.val.len = strlen(message);
    return response;
}

// If wizer is run on this module, these fields will be populated at build time and hence we'll be able
// to skip loading and initializing the runtime on a per-request basis. But if wizer isn't run, we'll
// set up the runtime separately for each request.
const char* preinitialized_error;
MonoMethod* preinitialized_handler;
MonoClass* preinitialized_builder_class;
MonoImage* preinitialized_sdk_image;
int preinitialized_done;

__attribute__((export_name("wizer.initialize")))
void ensure_preinitialized() {
    if (!preinitialized_done) {
        preinitialized_done = 1;

        dotnet_wasi_registerbundledassemblies();
        mono_wasm_load_runtime("", 0);

        entry_points_err_t entry_points_err = find_entry_points("Fermyon.Spin.Sdk.HttpHandlerAttribute", "HttpRequestInterop", &preinitialized_handler, &preinitialized_builder_class, &preinitialized_sdk_image);
        if (entry_points_err) {
            if (entry_points_err == EP_ERR_NO_HANDLER_METHOD) {
                preinitialized_error = "Assembly does not contain a method with HttpHandlerAttribute";
            } else {
                preinitialized_error = "Internal error loading HTTP handler";
            }
            return;
        }

        // To warm the interpreter, we need to run the main code path that is going to execute per-request. That way the preinitialized
        // binary is already ready to go at full speed.
        spin_http_request_t fake_req = {
            .method = SPIN_HTTP_METHOD_GET,
            .uri = { (void*)'/', 1 },
        };
        spin_http_response_t fake_res;
        spin_http_handle_http_request(&fake_req, &fake_res);
    }
}

void spin_http_handle_http_request(spin_http_request_t *req, spin_http_response_t *ret0) {
    ensure_preinitialized();

    if (preinitialized_error) {
        *ret0 = internal_error(preinitialized_error);
        return;
    }

    MonoObject* request_interop;
    if (wit_request_to_interop_request(preinitialized_sdk_image, preinitialized_builder_class, req, &request_interop) != CONV_ERR_OK) {
        *ret0 = internal_error("Internal error converting request to CLR object");
        return;
    }

    MonoObject* request_obj;
    if (interop_request_to_sdk_request(preinitialized_builder_class, request_interop, &request_obj) != CONV_ERR_OK) {
        *ret0 = internal_error("Internal error converting request to CLR object");
        return;
    }

    MonoObject* response_obj;
    MonoObject* exn;
    call_clr_request_handler(preinitialized_handler, request_obj, &response_obj, &exn);
    if (exn) {
        MonoString* exn_str = mono_object_to_string(exn, NULL);
        char* exn_cstr = mono_wasm_string_get_utf8(exn_str);
        *ret0 = internal_error(exn_cstr);
        return;
    }

    MonoObject* response_interop;
    if (sdk_response_to_interop_response(response_obj, &response_interop) != CONV_ERR_OK) {
        *ret0 = internal_error("Internal error converting response from CLR object");
        return;
    }
    spin_http_response_t resp;
    if (interop_response_to_wit_response(response_interop, &resp) != CONV_ERR_OK) {
        *ret0 = internal_error("Internal error converting response from CLR object");
        return;
    }

    *ret0 = resp;
}
