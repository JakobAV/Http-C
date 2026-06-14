#define HTTP_STATUS_LIST \
  X(Continue, 100, "Continue") \
  X(Switching_protocols, 101, "Switching protocols") \
  X(Processing, 102, "Processing") \
  X(Early_Hints, 103, "Early Hints") \
  X(OK, 200, "OK") \
  X(Created, 201, "Created") \
  X(Accepted, 202, "Accepted") \
  X(Non_Authoritative_Information, 203, "Non Authoritative Information") \
  X(No_Content, 204, "No Content") \
  X(Reset_Content, 205, "Reset Content") \
  X(Partial_Content, 206, "Partial Content") \
  X(Multi_Status, 207, "Multi Status") \
  X(Already_Reported, 208, "Already Reported") \
  X(IM_Used, 226, "IM Used") \
  X(Multiple_Choices, 300, "Multiple Choices") \
  X(Moved_Permanently, 301, "Moved Permanently") \
  X(Found, 302, "Found") \
  X(See_Other, 303, "See Other") \
  X(Not_Modified, 304, "Not Modified") \
  X(Use_Proxy, 305, "Use Proxy") \
  X(Switch_Proxy, 306, "Switch Proxy") \
  X(Temporary_Redirect, 307, "Temporary Redirect") \
  X(Permanent_Redirect, 308, "Permanent Redirect") \
  X(Bad_Request, 400, "Bad Request") \
  X(Unauthorized, 401, "Unauthorized") \
  X(Payment_Required, 402, "Payment Required") \
  X(Forbidden, 403, "Forbidden") \
  X(Not_Found, 404, "Not Found") \
  X(Method_Not_Allowed, 405, "Method Not Allowed") \
  X(Not_Acceptable, 406, "Not Acceptable") \
  X(Proxy_Authentication_Required, 407, "Proxy Authentication Required") \
  X(Request_Timeout, 408, "Request Timeout") \
  X(Conflict, 409, "Conflict") \
  X(Gone, 410, "Gone") \
  X(Length_Required, 411, "Length Required") \
  X(Precondition_Failed, 412, "Precondition Failed") \
  X(Payload_Too_Large, 413, "Payload Too Large") \
  X(URI_Too_Long, 414, "URI Too Long") \
  X(Unsupported_Media_Type, 415, "Unsupported Media Type") \
  X(Range_Not_Satisfiable, 416, "Range Not Satisfiable") \
  X(Expectation_Failed, 417, "Expectation Failed") \
  X(Im_a_Teapot, 418, "Im a Teapot") \
  X(Misdirected_Request, 421, "Misdirected Request") \
  X(Unprocessable_Entity, 422, "Unprocessable Entity") \
  X(Locked, 423, "Locked") \
  X(Failed_Dependency, 424, "Failed Dependency") \
  X(Too_Early, 425, "Too Early") \
  X(Upgrade_Required, 426, "Upgrade Required") \
  X(Precondition_Required, 428, "Precondition Required") \
  X(Too_Many_Requests, 429, "Too Many Requests") \
  X(Request_Header_Fields_Too_Large, 431, "Request Header Fields Too Large") \
  X(Unavailable_For_Legal_Reasons, 451, "Unavailable For Legal Reasons") \
  X(Internal_Server_Error, 500, "Internal Server Error") \
  X(Not_Implemented, 501, "Not Implemented") \
  X(Bad_Gateway, 502, "Bad Gateway") \
  X(Service_Unavailable, 503, "Service Unavailable") \
  X(Gateway_Timeout, 504, "Gateway Timeout") \
  X(HTTP_Version_Not_Supported, 505, "HTTP Version Not Supported") \
  X(Variant_Also_Negotiates, 506, "Variant Also Negotiates") \
  X(Insufficient_Storage, 507, "Insufficient Storage") \
  X(Loop_Detected, 508, "Loop Detected") \
  X(Not_Extended, 510, "Not Extended") \
  X(Network_Authentication_Required, 511, "Network Authentication Required")

typedef enum HttpStatus
{
#define X(name, code, _) HttpStatus_##name = code,
  HTTP_STATUS_LIST
#undef X
} HttpStatus;

String http_status_to_string(HttpStatus status)
{
    switch (status)
    {
#define X(name, code, friendly_name) case HttpStatus_##name: return STR_LIT(#code " " friendly_name);
        HTTP_STATUS_LIST
#undef X
        default: InvalidCodePath;
    }
}