#define PyExpat_CAPI_MAGIC  "pyexpat.expat_CAPI 1.0"
#define PyExpat_CAPSULE_NAME "pyexpat.expat_CAPI"

struct PyExpat_CAPI
{
    char* magic;
    int size;
    int MAJOR_VERSION;
    int MINOR_VERSION;
    int MICRO_VERSION;

    const XML_LChar * (*ErrorString)(enum XML_Error code);
    enum XML_Error (*GetErrorCode)(XML_Parser parser);
    XML_Size (*GetErrorColumnNumber)(XML_Parser parser);
    XML_Size (*GetErrorLineNumber)(XML_Parser parser);
    enum XML_Status (*Parse)(
        XML_Parser parser, const char *s, int len, int isFinal);
    XML_Parser (*ParserCreate_MM)(
        const XML_Char *encoding, const XML_Memory_Handling_Suite *memsuite,
        const XML_Char *namespaceSeparator);
    void (*ParserFree)(XML_Parser parser);
    void (*SetCharacterDataHandler)(
        XML_Parser parser, XML_CharacterDataHandler handler);
    void (*SetCommentHandler)(
        XML_Parser parser, XML_CommentHandler handler);
    void (*SetDefaultHandlerExpand)(
        XML_Parser parser, XML_DefaultHandler handler);
    void (*SetElementHandler)(
        XML_Parser parser, XML_StartElementHandler start,
        XML_EndElementHandler end);
    void (*SetNamespaceDeclHandler)(
        XML_Parser parser, XML_StartNamespaceDeclHandler start,
        XML_EndNamespaceDeclHandler end);
    void (*SetProcessingInstructionHandler)(
        XML_Parser parser, XML_ProcessingInstructionHandler handler);
    void (*SetUnknownEncodingHandler)(
        XML_Parser parser, XML_UnknownEncodingHandler handler,
        void *encodingHandlerData);
    void (*SetUserData)(XML_Parser parser, void *userData);

};
