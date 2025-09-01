#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "lib/llhttp.h"

#define STRING(x) #x
#define XSTRING(x) STRING(x)
#define LLHTTP_VERSION XSTRING(LLHTTP_VERSION_MAJOR) "." XSTRING(LLHTTP_VERSION_MINOR) "." XSTRING(LLHTTP_VERSION_PATCH)

typedef struct {
    PyObject_HEAD
    llhttp_t llhttp;
} parser_object;

static PyObject *base_error;
static PyObject *errors[] = {
#define HTTP_ERRNO_GEN(CODE, NAME, _) [CODE] = NULL,
HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
#undef HTTP_ERRNO_GEN
};

static PyObject *methods[] = {
#define HTTP_METHOD_GEN(NUMBER, NAME, STRING) [NUMBER] = NULL,
HTTP_ALL_METHOD_MAP(HTTP_METHOD_GEN)
#undef HTTP_METHOD_GEN
};


static int
parser_callback(PyObject *name, llhttp_t *llhttp) {
    PyObject *result = PyObject_CallMethodNoArgs(llhttp->data, name);
    if (result)
        Py_DECREF(result);

    if (PyErr_Occurred())
        return HPE_USER;

    if (HPE_PAUSED == llhttp_get_errno(llhttp)) {
        llhttp_resume(llhttp);
        return HPE_PAUSED;
    }

    if (HPE_PAUSED_UPGRADE == llhttp_get_errno(llhttp)) {
        llhttp_resume_after_upgrade(llhttp);
        return HPE_PAUSED_UPGRADE;
    }

    return HPE_OK;
}

static int
parser_data_callback(PyObject *name, llhttp_t *llhttp, const char *data, size_t length) {
    PyObject *payload = PyMemoryView_FromMemory((char*)data, length, PyBUF_READ);
    PyObject *result = PyObject_CallMethodOneArg(llhttp->data, name, payload);
    Py_DECREF(payload);
    if (result)
        Py_DECREF(result);

    if (PyErr_Occurred())
        return HPE_USER;

    if (HPE_PAUSED == llhttp_get_errno(llhttp)) {
        llhttp_resume(llhttp);
        return HPE_PAUSED;
    }

    if (HPE_PAUSED_UPGRADE == llhttp_get_errno(llhttp)) {
        llhttp_resume_after_upgrade(llhttp);
        return HPE_PAUSED_UPGRADE;
    }

    return HPE_OK;
}

#define PARSER_CALLBACK(type) \
static int parser_ ## type (llhttp_t *llhttp) \
{ \
    static PyObject * name = NULL; \
    if (!name) \
        name = PyUnicode_InternFromString(#type); \
    return parser_callback(name, llhttp); \
}

#define PARSER_DATA_CALLBACK(type) \
static int parser_ ## type (llhttp_t *llhttp, const char *data, size_t length) \
{ \
    static PyObject * name = NULL; \
    if (!name) \
        name = PyUnicode_InternFromString(#type); \
    return parser_data_callback(name, llhttp, data, length); \
}

PARSER_CALLBACK(on_message_begin);
PARSER_DATA_CALLBACK(on_protocol);
PARSER_DATA_CALLBACK(on_url);
PARSER_DATA_CALLBACK(on_status);
PARSER_DATA_CALLBACK(on_method);
PARSER_DATA_CALLBACK(on_version);
PARSER_DATA_CALLBACK(on_header_field);
PARSER_DATA_CALLBACK(on_header_value);
PARSER_DATA_CALLBACK(on_chunk_extension_name);
PARSER_DATA_CALLBACK(on_chunk_extension_value);
PARSER_CALLBACK(on_headers_complete);
PARSER_DATA_CALLBACK(on_body);
PARSER_CALLBACK(on_message_complete);
PARSER_CALLBACK(on_protocol_complete);
PARSER_CALLBACK(on_url_complete);
PARSER_CALLBACK(on_status_complete);
PARSER_CALLBACK(on_method_complete);
PARSER_CALLBACK(on_version_complete);
PARSER_CALLBACK(on_header_field_complete);
PARSER_CALLBACK(on_header_value_complete);
PARSER_CALLBACK(on_chunk_extension_name_complete);
PARSER_CALLBACK(on_chunk_extension_value_complete);
PARSER_CALLBACK(on_chunk_header);
PARSER_CALLBACK(on_chunk_complete);
PARSER_CALLBACK(on_reset);

#define PARSER_SETTING(type) . type = parser_ ##type
llhttp_settings_t parser_settings = {
    PARSER_SETTING(on_message_begin),
    PARSER_SETTING(on_protocol),
    PARSER_SETTING(on_url),
    PARSER_SETTING(on_status),
    PARSER_SETTING(on_method),
    PARSER_SETTING(on_version),
    PARSER_SETTING(on_header_field),
    PARSER_SETTING(on_header_value),
    PARSER_SETTING(on_chunk_extension_name),
    PARSER_SETTING(on_chunk_extension_value),
    PARSER_SETTING(on_headers_complete),
    PARSER_SETTING(on_body),
    PARSER_SETTING(on_message_complete),
    PARSER_SETTING(on_protocol_complete),
    PARSER_SETTING(on_url_complete),
    PARSER_SETTING(on_status_complete),
    PARSER_SETTING(on_method_complete),
    PARSER_SETTING(on_version_complete),
    PARSER_SETTING(on_header_field_complete),
    PARSER_SETTING(on_header_value_complete),
    PARSER_SETTING(on_chunk_extension_name_complete),
    PARSER_SETTING(on_chunk_extension_value_complete),
    PARSER_SETTING(on_chunk_header),
    PARSER_SETTING(on_chunk_complete),
    PARSER_SETTING(on_reset),
};

static PyObject *
request_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PyObject *self = type->tp_alloc(type, 0);
    if (self) {
        llhttp_t *llhttp = &((parser_object*)self)->llhttp;
        llhttp_init(llhttp, HTTP_REQUEST, &parser_settings);
        llhttp->data = self;
    }
    return self;
}

static PyObject *
response_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    PyObject *self = type->tp_alloc(type, 0);
    if (self) {
        llhttp_t *llhttp = &((parser_object*)self)->llhttp;
        llhttp_init(llhttp, HTTP_RESPONSE, &parser_settings);
        llhttp->data = self;
    }
    return self;
}

static PyObject *
parser_execute(PyObject *self, PyObject *payload) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;

    Py_buffer buffer;
    if (PyObject_GetBuffer(payload, &buffer, PyBUF_SIMPLE))
        return NULL;

    if (!PyBuffer_IsContiguous(&buffer, 'C')) {
        PyErr_SetString(PyExc_TypeError, "buffer is not contiguous");
        PyBuffer_Release(&buffer);
        return NULL;
    }

    llhttp_errno_t error = llhttp_execute(llhttp, buffer.buf, buffer.len);
    PyBuffer_Release(&buffer);

    if (PyErr_Occurred())
        return NULL;

    switch (error) {
    case HPE_OK:
        return PyLong_FromUnsignedLong(buffer.len);

    case HPE_PAUSED:
    case HPE_PAUSED_UPGRADE:
    case HPE_PAUSED_H2_UPGRADE:
        return PyLong_FromUnsignedLong(llhttp->error_pos - (const char*)buffer.buf);

    default:
        PyErr_SetString(errors[error], llhttp_get_error_reason(llhttp));
        return NULL;
    }
}

static PyObject *
parser_pause(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_pause(llhttp);
    Py_RETURN_NONE;
}

static PyObject *
parser_unpause(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_resume(llhttp);
    Py_RETURN_NONE;
}

static PyObject *
parser_upgrade(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_resume_after_upgrade(llhttp);
    Py_RETURN_NONE;
}

static PyObject *
parser_finish(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;

    llhttp_errno_t error = llhttp_finish(llhttp);
    if (HPE_OK == error)
        Py_RETURN_NONE;

    PyErr_SetString(errors[error], llhttp_get_error_reason(llhttp));
    return NULL;
}

static PyObject *
parser_reset(PyObject *self) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    llhttp_reset(llhttp);
    Py_RETURN_NONE;
}

static PyObject * parser_dummy_noargs(PyObject *self) { Py_RETURN_NONE; }
static PyObject * parser_dummy_onearg(PyObject *self, PyObject *arg) { Py_RETURN_NONE; }

static PyMethodDef parser_methods[] = {
    { "execute", (PyCFunction)parser_execute, METH_O },
    { "pause", (PyCFunction)parser_pause, METH_NOARGS },
    { "unpause", (PyCFunction)parser_unpause, METH_NOARGS },
    { "upgrade", (PyCFunction)parser_upgrade, METH_NOARGS },
    { "finish", (PyCFunction)parser_finish, METH_NOARGS },
    { "reset", (PyCFunction)parser_reset, METH_NOARGS },
    { "on_message_begin", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_protocol", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_url", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_status", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_method", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_version", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_header_field", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_header_value", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_chunk_extension_name", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_chunk_extension_value", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_headers_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_body", (PyCFunction)parser_dummy_onearg, METH_O },
    { "on_message_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_protocol_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_url_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_status_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_method_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_version_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_header_field_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_header_value_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_chunk_extension_name_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_chunk_extension_value_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_chunk_header", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_chunk_complete", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { "on_reset", (PyCFunction)parser_dummy_noargs, METH_NOARGS },
    { NULL }
};

static PyObject *
parser_method(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (llhttp->type != HTTP_REQUEST)
        Py_RETURN_NONE;
    if (!llhttp->http_major && !llhttp->http_minor)
        Py_RETURN_NONE;

    PyObject * method = methods[llhttp->method];
    Py_INCREF(method);
    return method;
}

static PyObject *
parser_major(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (!llhttp->http_major && !llhttp->http_minor)
        Py_RETURN_NONE;

    return PyLong_FromUnsignedLong(llhttp->http_major);
}

static PyObject *
parser_minor(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (!llhttp->http_major && !llhttp->http_minor)
        Py_RETURN_NONE;

    return PyLong_FromUnsignedLong(llhttp->http_minor);
}

static PyObject *
parser_status(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (!llhttp->status_code)
        Py_RETURN_NONE;

    return PyLong_FromUnsignedLong(llhttp->status_code);
}

static PyObject *
parser_content_length(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (!(llhttp->flags & F_CONTENT_LENGTH))
        Py_RETURN_NONE;

    return PyLong_FromUnsignedLong(llhttp->content_length);
}

static bool
get_lenient(const llhttp_t *llhttp, llhttp_lenient_flags_t flag) {
    return llhttp->lenient_flags & flag;
}

static int
set_lenient(llhttp_t *llhttp, llhttp_lenient_flags_t flag, bool value) {
    if (value) {
        llhttp->lenient_flags |= flag;
    } else {
        llhttp->lenient_flags &= ~flag;
    }
    return 0;
}

#define LENIENT_FLAG(name) \
static PyObject * \
parser_get_lenient_ ## name(PyObject *self, void *closure) \
    { return PyBool_FromLong(get_lenient(&((parser_object*)self)->llhttp, LENIENT_ ## name)); } \
\
static int \
parser_set_lenient_ ## name(PyObject *self, PyObject *value, void *closure) \
    { return set_lenient(&((parser_object*)self)->llhttp, LENIENT_ ## name, PyObject_IsTrue(value)); }

LENIENT_FLAG(HEADERS);
LENIENT_FLAG(CHUNKED_LENGTH);
LENIENT_FLAG(KEEP_ALIVE);
LENIENT_FLAG(TRANSFER_ENCODING);
LENIENT_FLAG(VERSION);
LENIENT_FLAG(DATA_AFTER_CLOSE);
LENIENT_FLAG(OPTIONAL_LF_AFTER_CR);
LENIENT_FLAG(OPTIONAL_CRLF_AFTER_CHUNK);
LENIENT_FLAG(OPTIONAL_CR_BEFORE_LF);
LENIENT_FLAG(SPACES_AFTER_CHUNK_SIZE);

static PyObject *
parser_message_needs_eof(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(llhttp_message_needs_eof(llhttp));
}

static PyObject *
parser_should_keep_alive(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(llhttp_should_keep_alive(llhttp));
}

static PyObject *
parser_is_paused(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    return PyBool_FromLong(HPE_PAUSED == llhttp_get_errno(llhttp));
}

static PyObject *
parser_is_upgrading(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    switch (llhttp_get_errno(llhttp)) {
    case HPE_PAUSED_UPGRADE:
    case HPE_PAUSED_H2_UPGRADE:
        Py_RETURN_TRUE;
        break;
    default:
         Py_RETURN_FALSE;
         break;
    }
}

static PyObject *
parser_is_busted(PyObject *self, void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    switch (llhttp_get_errno(llhttp)) {
    case HPE_OK:
    case HPE_PAUSED:
    case HPE_PAUSED_UPGRADE:
        Py_RETURN_FALSE;
    default:
        Py_RETURN_TRUE;
    }
}

static PyObject *
parser_error(PyObject *self,  void *closure) {
    llhttp_t *llhttp = &((parser_object*)self)->llhttp;
    if (HPE_OK == llhttp_get_errno(llhttp))
        Py_RETURN_NONE;
    return PyUnicode_FromString(llhttp_get_error_reason(llhttp));
}

static PyGetSetDef parser_getset[] = {
    { "method", parser_method },
    { "major", parser_major },
    { "minor", parser_minor },
    { "status", parser_status },
    { "content_length", parser_content_length },
    { "lenient_headers", parser_get_lenient_HEADERS, parser_set_lenient_HEADERS },
    { "lenient_chunked_length", parser_get_lenient_CHUNKED_LENGTH, parser_set_lenient_CHUNKED_LENGTH },
    { "lenient_keep_alive", parser_get_lenient_KEEP_ALIVE, parser_set_lenient_KEEP_ALIVE },
    { "lenient_transfer_encoding", parser_get_lenient_TRANSFER_ENCODING, parser_set_lenient_TRANSFER_ENCODING },
    { "lenient_version", parser_get_lenient_VERSION, parser_set_lenient_VERSION },
    { "lenient_data_after_close", parser_get_lenient_DATA_AFTER_CLOSE, parser_set_lenient_DATA_AFTER_CLOSE },
    { "lenient_lf_after_cr", parser_get_lenient_OPTIONAL_LF_AFTER_CR, parser_set_lenient_OPTIONAL_LF_AFTER_CR },
    { "lenient_optional_crlf_after_chunk", parser_get_lenient_OPTIONAL_CRLF_AFTER_CHUNK, parser_set_lenient_OPTIONAL_CRLF_AFTER_CHUNK },
    { "lenient_optional_cr_before_lf", parser_get_lenient_OPTIONAL_CR_BEFORE_LF, parser_set_lenient_OPTIONAL_CR_BEFORE_LF },
    { "lenient_spaces_after_chunk_size", parser_get_lenient_SPACES_AFTER_CHUNK_SIZE, parser_set_lenient_SPACES_AFTER_CHUNK_SIZE },
    { "message_needs_eof", parser_message_needs_eof },
    { "should_keep_alive", parser_should_keep_alive },
    { "is_paused", parser_is_paused },
    { "is_upgrading", parser_is_upgrading },
    { "is_busted", parser_is_busted },
    { "error", parser_error },
    { NULL }
};

static void
parser_dealloc(PyObject *self) {
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyType_Slot request_slots[] = {
    {Py_tp_doc, "llhttp request parser"},
    {Py_tp_new, request_new},
    {Py_tp_dealloc, parser_dealloc},
    {Py_tp_methods, parser_methods},
    {Py_tp_getset, parser_getset},
    {0, 0},
};

static PyType_Spec request_spec = {
    "llhttp.Request",
    sizeof(parser_object),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    request_slots,
};

static PyType_Slot response_slots[] = {
    {Py_tp_doc, "llhttp response parser"},
    {Py_tp_new, response_new},
    {Py_tp_dealloc, parser_dealloc},
    {Py_tp_methods, parser_methods},
    {Py_tp_getset, parser_getset},
    {0, 0},
};

static PyType_Spec response_spec = {
    "llhttp.Response",
    sizeof(parser_object),
    0,
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    response_slots,
};

static struct PyModuleDef llhttp_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "llhttp",
    .m_doc = "llhttp wrapper",
    .m_size = -1,
};

static char *
snake_to_camel(char * string) {
    bool upper = true;
    char * camel = string;
    for (const char * snake = string ; *snake ; ++snake) {
        if (isalpha(*snake)) {
            *camel++ = upper ? toupper(*snake) : tolower(*snake);
        } else if (isdigit(*snake)) {
            *camel++ = *snake;
        }
        upper = !isalpha(*snake);
    }
    *camel = '\0';
    return string;
}

PyMODINIT_FUNC
PyInit___llhttp(void) {
    PyObject *m = PyModule_Create(&llhttp_module);
    if (!m)
        return NULL;

    if (PyModule_AddStringConstant(m, "version", LLHTTP_VERSION))
        goto fail;

    if ((base_error = PyErr_NewException("llhttp.Error", NULL, NULL))) {
        Py_INCREF(base_error);
        PyModule_AddObject(m, "Error", base_error);

#define HTTP_ERRNO_GEN(CODE, NAME, _) \
        if (CODE != HPE_OK && CODE != HPE_PAUSED && CODE != HPE_PAUSED_UPGRADE) { \
            char long_name[] = "llhttp." #NAME "_Error"; \
            char *short_name = snake_to_camel(long_name + strlen("llhttp.")); \
            if ((errors[CODE] = PyErr_NewException(long_name, base_error, NULL))) { \
                Py_INCREF(errors[CODE]); \
                PyModule_AddObject(m, short_name, errors[CODE]); \
            } \
        }
HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
#undef HTTP_ERRNO_GEN

    }

#define HTTP_METHOD_GEN(NUMBER, NAME, STRING) \
    methods[HTTP_ ## NAME] = PyUnicode_FromStringAndSize(#STRING, strlen(#STRING));
HTTP_ALL_METHOD_MAP(HTTP_METHOD_GEN)
#undef HTTP_METHOD_GEN

    PyObject *request_type = PyType_FromSpec(&request_spec);
    if (!request_type)
        goto fail;

    if (PyModule_AddObject(m, request_spec.name + strlen("llhttp."), request_type)) {
        Py_DECREF(request_type);
        goto fail;
    }

    PyObject *response_type = PyType_FromSpec(&response_spec);
    if (!response_type)
        goto fail;

    if (PyModule_AddObject(m, response_spec.name + strlen("llhttp."), response_type)) {
        Py_DECREF(response_type);
        goto fail;
    }

    return m;

fail:
    Py_DECREF(m);
    return NULL;
}

//
