/* Typemap definitions, to allow SWIG to properly handle 'char**' data types. */

%typemap(in) char ** {
  /* Check if is a list  */
  if (PyList_Check($input)) {
    int size = PyList_Size($input);
    int i = 0;
    $1 = (char **) malloc((size+1) * sizeof(char*));
    for (i = 0; i < size; i++) {
      PyObject *o = PyList_GetItem($input,i);
      if (PyString_Check(o))
        $1[i] = PyString_AsString(o);
      else {
        PyErr_SetString(PyExc_TypeError,"list must contain strings");
        free($1);
        return NULL;
      }
    }
    $1[i] = 0;
  } else if ($input == Py_None) {
    $1 =  NULL;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

%typemap(freearg) char** {
  free((char *) $1);
}

%typemap(out) char** {
  int len;
  int i;
  len = 0;
  while ($1[len]) len++;
  $result = PyList_New(len);
  for (i = 0; i < len; i++) {
    PyList_SetItem($result, i, PyString_FromString($1[i]));
  }
}

/* Typemap definitions to allow SWIG to properly handle char buffer. */

// typemap for a char buffer
// See also SBThread::GetStopDescription.
%typemap(in) (char *dst, size_t dst_len) {
   if (!PyInt_Check($input)) {
       PyErr_SetString(PyExc_ValueError, "Expecting an integer");
       return NULL;
   }
   $2 = PyInt_AsLong($input);
   if ($2 <= 0) {
       PyErr_SetString(PyExc_ValueError, "Positive integer expected");
       return NULL;
   }
   $1 = (char *) malloc($2);
}

// Return the char buffer.  Discarding any previous return result
// See also SBThread::GetStopDescription.
%typemap(argout) (char *dst, size_t dst_len) {
   Py_XDECREF($result);   /* Blow away any previous result */
   $result = PyString_FromStringAndSize(($1),result);
   free($1);
}


// typemap for an outgoing buffer
// See also SBEvent::SBEvent(uint32_t event, const char *cstr, uint32_t cstr_len).
%typemap(in) (const char *cstr, uint32_t cstr_len) {
   if (PyString_Check($input)) {
      $1 = (char *) PyString_AsString($input);
      $2 = PyString_Size($input);
   }
   else if(PyByteArray_Check($input)) {
      $1 = (char *) PyByteArray_AsString($input);
      $2 = PyByteArray_Size($input);
   }
   else {
      PyErr_SetString(PyExc_ValueError, "Expecting a string");
      return NULL;
   }
}
// Ditto for SBProcess::PutSTDIN(const char *src, size_t src_len).
%typemap(in) (const char *src, size_t src_len) {
   if (PyString_Check($input)) {
      $1 = (char *) PyString_AsString($input);
      $2 = PyString_Size($input);
   }
   else if(PyByteArray_Check($input)) {
      $1 = (char *) PyByteArray_AsString($input);
      $2 = PyByteArray_Size($input);
   }
   else {
      PyErr_SetString(PyExc_ValueError, "Expecting a string");
      return NULL;
   }
}
// And SBProcess::WriteMemory.
%typemap(in) (const void *buf, size_t size) {
   if (PyString_Check($input)) {
      $1 = (void *) PyString_AsString($input);
      $2 = PyString_Size($input);
   }
   else if(PyByteArray_Check($input)) {
      $1 = (void *) PyByteArray_AsString($input);
      $2 = PyByteArray_Size($input);
   }
   else {
      PyErr_SetString(PyExc_ValueError, "Expecting a string");
      return NULL;
   }
}

// typemap for an incoming buffer
// See also SBProcess::ReadMemory.
%typemap(in) (void *buf, size_t size) {
   if (!PyInt_Check($input)) {
       PyErr_SetString(PyExc_ValueError, "Expecting an integer");
       return NULL;
   }
   $2 = PyInt_AsLong($input);
   if ($2 <= 0) {
       PyErr_SetString(PyExc_ValueError, "Positive integer expected");
       return NULL;
   }
   $1 = (void *) malloc($2);
}

// Return the buffer.  Discarding any previous return result
// See also SBProcess::ReadMemory.
%typemap(argout) (void *buf, size_t size) {
   Py_XDECREF($result);   /* Blow away any previous result */
   $result = PyString_FromStringAndSize(static_cast<const char*>($1),result);
   free($1);
}

// these typemaps allow Python users to pass list objects
// and have them turn into C++ arrays (this is useful, for instance
// when creating SBData objects from lists of numbers)
%typemap(in) (uint64_t* array, size_t array_len) {
  /* Check if is a list  */
  if (PyList_Check($input)) {
    int size = PyList_Size($input);
    int i = 0;
    $2 = size;
    $1 = (uint64_t*) malloc(size * sizeof(uint64_t));
    for (i = 0; i < size; i++) {
      PyObject *o = PyList_GetItem($input,i);
      if (PyInt_Check(o)) {
        $1[i] = PyInt_AsLong(o);
      }
      else {
        PyErr_SetString(PyExc_TypeError,"list must contain numbers");
        free($1);
        return NULL;
      }
    }
  } else if ($input == Py_None) {
    $1 =  NULL;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

%typemap(freearg) (uint64_t* array, size_t array_len) {
  free($1);
}

%typemap(in) (uint32_t* array, size_t array_len) {
  /* Check if is a list  */
  if (PyList_Check($input)) {
    int size = PyList_Size($input);
    int i = 0;
    $2 = size;
    $1 = (uint32_t*) malloc(size * sizeof(uint32_t));
    for (i = 0; i < size; i++) {
      PyObject *o = PyList_GetItem($input,i);
      if (PyInt_Check(o)) {
        $1[i] = PyInt_AsLong(o);
      }
      else {
        PyErr_SetString(PyExc_TypeError,"list must contain numbers");
        free($1);
        return NULL;
      }
    }
  } else if ($input == Py_None) {
    $1 =  NULL;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

%typemap(freearg) (uint32_t* array, size_t array_len) {
  free($1);
}

%typemap(in) (int64_t* array, size_t array_len) {
  /* Check if is a list  */
  if (PyList_Check($input)) {
    int size = PyList_Size($input);
    int i = 0;
    $2 = size;
    $1 = (int64_t*) malloc(size * sizeof(int64_t));
    for (i = 0; i < size; i++) {
      PyObject *o = PyList_GetItem($input,i);
      if (PyInt_Check(o)) {
        $1[i] = PyInt_AsLong(o);
      }
      else {
        PyErr_SetString(PyExc_TypeError,"list must contain numbers");
        free($1);
        return NULL;
      }
    }
  } else if ($input == Py_None) {
    $1 =  NULL;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

%typemap(freearg) (int64_t* array, size_t array_len) {
  free($1);
}

%typemap(in) (int32_t* array, size_t array_len) {
  /* Check if is a list  */
  if (PyList_Check($input)) {
    int size = PyList_Size($input);
    int i = 0;
    $2 = size;
    $1 = (int32_t*) malloc(size * sizeof(int32_t));
    for (i = 0; i < size; i++) {
      PyObject *o = PyList_GetItem($input,i);
      if (PyInt_Check(o)) {
        $1[i] = PyInt_AsLong(o);
      }
      else {
        PyErr_SetString(PyExc_TypeError,"list must contain numbers");
        free($1);
        return NULL;
      }
    }
  } else if ($input == Py_None) {
    $1 =  NULL;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

%typemap(freearg) (int32_t* array, size_t array_len) {
  free($1);
}

%typemap(in) (double* array, size_t array_len) {
  /* Check if is a list  */
  if (PyList_Check($input)) {
    int size = PyList_Size($input);
    int i = 0;
    $2 = size;
    $1 = (double*) malloc(size * sizeof(double));
    for (i = 0; i < size; i++) {
      PyObject *o = PyList_GetItem($input,i);
      if (PyFloat_Check(o)) {
        $1[i] = PyFloat_AsDouble(o);
      }
      else {
        PyErr_SetString(PyExc_TypeError,"list must contain floating-point numbers");
        free($1);
        return NULL;
      }
    }
  } else if ($input == Py_None) {
    $1 =  NULL;
  } else {
    PyErr_SetString(PyExc_TypeError,"not a list");
    return NULL;
  }
}

%typemap(freearg) (double* array, size_t array_len) {
  free($1);
}
