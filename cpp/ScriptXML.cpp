#include "ScriptXML.h"
#include <assert.h> // assert()
#include "tinyxml2/tinyxml2.h"
#include "add_on/scriptdictionary/scriptdictionary.h"
#include "add_on/scriptarray/scriptarray.h"
#include "add_on/autowrapper/aswrappedcall.h"

using namespace tinyxml2;

enum ASXMLNodeType
{
    kXmlElement,
    kXmlComment,
    kXmlText
};

class ASXMLNode
{
public:
    ASXMLNode(asIScriptEngine* engine) :
        attributes(NULL),
        children(NULL),
        refCount(1),
        type(kXmlElement)
    {
        attributes=CScriptDictionary::Create(engine);
        children=CScriptArray::Create(engine->GetTypeInfoByDecl("array<XmlNode@>"));
    }
public:
    ASXMLNodeType       type;
    std::string         name;
    CScriptDictionary*  attributes;
    CScriptArray*       children;

    // not exposed
    int refCount;
    void AddRef()
    {
        refCount++;
    }
    void Release()
    {
        refCount--;
        if(refCount==0)
        {
            delete this;
        }
    }
private:
    ~ASXMLNode()
    {
        attributes->Release();
        attributes=NULL;
        children->Release();
        children=NULL;
    }
};

// conversion
static ASXMLNode* TinyXMLToASXML(const XMLNode* inNode,asIScriptEngine* engine,int stringTypeId)
{
    ASXMLNode* newNode=NULL;
    if (engine!=NULL && inNode != NULL)
    {
        // create new node and copy name & type
        newNode=new ASXMLNode(engine);
        newNode->name=inNode->Value();
        if(inNode->ToComment())
            newNode->type=kXmlComment;
        else if(inNode->ToText())
            newNode->type=kXmlText;
        else
        {
            const XMLElement* element = inNode->ToElement();
            if (element)
            {
                newNode->type = kXmlElement;

                // copy attributes
                const XMLAttribute* attr = element->FirstAttribute();
                while (attr != NULL)
                {
                    const char* name = attr->Name();
                    std::string value = attr->Value();
                    newNode->attributes->Set(name, &value, stringTypeId);
                    attr = attr->Next();
                }

                // add children nodes
                const XMLNode* node = element->FirstChild();
                while (node != NULL)
                {
                    ASXMLNode* childNode = TinyXMLToASXML(node, engine, stringTypeId);
                    if (childNode)
                    {
                        newNode->children->InsertLast(&childNode);
                        childNode->Release();
                    }
                    node = node->NextSibling();
                }
            }
        }
    }
    return newNode;
}

static XMLNode* ASXMLToTinyXML(const ASXMLNode* element,XMLDocument* doc,int stringTypeId,bool sort)
{
    XMLNode* newNode=NULL;
    if (doc != NULL && element != NULL)
    {
        switch (element->type)
        {
        case kXmlComment:
        {
            newNode = doc->NewComment(element->name.c_str());
            break;
        }
        case kXmlText:
        {
            newNode = doc->NewText(element->name.c_str());
            break;
        }
        case kXmlElement:
        {
            // create new node and copy name
            XMLElement* newElement = doc->NewElement(element->name.c_str());

            // copy attributes (either sorted or not)
            if (sort)
            {
                CScriptArray* keys = element->attributes->GetKeys();
                if (keys != NULL)
                {
                    keys->SortAsc();
                    for (asUINT i = 0; i < keys->GetSize(); i++)
                    {
                        std::string value;
                        const std::string* name = (const std::string*)keys->At(i);
                        if (name)
                        {
                            if (element->attributes->Get(*name, (void*)&value, stringTypeId))
                            {
                                newElement->SetAttribute(name->c_str(), value.c_str());
                            }
                        }
                    }
                    keys->Release();
                }
            }
            else
            {
                for (auto iter = element->attributes->begin(); iter!=element->attributes->end(); iter++)
                {
                    std::string value;
                    const std::string& name = iter.GetKey();
                    if (!name.empty())
                    {
                        if (iter.GetValue((void*)&value, stringTypeId))
                        {
                            newElement->SetAttribute(name.c_str(), value.c_str());
                        }
                    }
                }
            }

            // add children nodes
            for (asUINT i = 0; i < element->children->GetSize(); i++)
            {
                ASXMLNode** node = (ASXMLNode**)element->children->At(i);
                if (node && *node)
                {
                    XMLNode* childElement = ASXMLToTinyXML(*node, doc, stringTypeId,sort);
                    if (childElement)
                    {
                        newElement->LinkEndChild(childElement);
                    }
                }
            }
            newNode = newElement;
            break;
        }
        }
    }
    return newNode;
}

static ASXMLNode* ASXMLParseFile(const std::string& file)
{
    XMLDocument doc;
    bool ok=doc.LoadFile(file.c_str())==XML_SUCCESS;
    if (ok)
    {
        asIScriptContext * currentContext=asGetActiveContext();
        if (currentContext)
        {
            asIScriptEngine* engine=currentContext->GetEngine();
            if (engine)
            {
                return TinyXMLToASXML(doc.RootElement(),engine,engine->GetTypeIdByDecl("string"));
            }
        }
    }
    return NULL;
}

static ASXMLNode* ASXMLParse(const std::string& str)
{
    XMLDocument doc;
    bool ok=doc.Parse(str.c_str())==XML_SUCCESS;
    if (ok)
    {
        asIScriptContext * currentContext=asGetActiveContext();
        if (currentContext)
        {
            asIScriptEngine* engine=currentContext->GetEngine();
            if (engine)
            {
                return TinyXMLToASXML(doc.RootElement(),engine,engine->GetTypeIdByDecl("string"));
            }
        }
    }
    return NULL;
}

// XML to text
static bool ASXMLWriteFile(const ASXMLNode& node,const std::string& file,bool sortAttributes)
{
    XMLDocument doc;
    bool ok=false;
    asIScriptContext * currentContext=asGetActiveContext();
    if (currentContext)
    {
        asIScriptEngine* engine=currentContext->GetEngine();
        if (engine)
        {
            XMLNode* elem=ASXMLToTinyXML(&node,&doc,engine->GetTypeIdByDecl("string"),sortAttributes);
            if (elem)
            {
                XMLDeclaration * decl = doc.NewDeclaration();
                doc.LinkEndChild( decl );
                doc.LinkEndChild( elem );
                ok=doc.SaveFile(file.c_str())==XML_SUCCESS;
            }
        }
    }
    return ok;
}

static bool ASXMLWrite(ASXMLNode& node, std::string& content,bool sortAttributes)
{
    XMLDocument doc;
    bool ok=false;
    asIScriptContext * currentContext = asGetActiveContext();
    if (currentContext)
    {
        asIScriptEngine* engine = currentContext->GetEngine();
        if (engine)
        {
            XMLNode* elem = ASXMLToTinyXML(&node, &doc, engine->GetTypeIdByDecl("string"),sortAttributes);
            if (elem)
            {
                XMLDeclaration * decl = doc.NewDeclaration();
                doc.LinkEndChild(decl);
                doc.LinkEndChild(elem);
                XMLPrinter printer(NULL, true);
                doc.Print(&printer);
                content = printer.CStr();
            }
        }
    }
    return ok;
}

void XmlNodeFactory_Generic(asIScriptGeneric *gen)
{
    *(ASXMLNode**)gen->GetAddressOfReturnLocation() = new ASXMLNode(gen->GetEngine());
}

// registration
static void RegisterScriptXML_Native(asIScriptEngine *engine)
{
    int r=0;
    // XMLNodeType enum
    r = engine->RegisterEnum("XmlNodeType"); assert(r>=0);
    r = engine->RegisterEnumValue("XmlNodeType", "kXmlElement", kXmlElement);assert(r>=0);
    r = engine->RegisterEnumValue("XmlNodeType", "kXmlComment", kXmlComment);assert(r>=0);
    r = engine->RegisterEnumValue("XmlNodeType", "kXmlText", kXmlText);assert(r>=0);

    // XMLNode class
    r = engine->RegisterObjectType("XmlNode",sizeof(ASXMLNode), asOBJ_REF); assert( r >= 0 );
    r = engine->RegisterObjectBehaviour("XmlNode", asBEHAVE_FACTORY, "XmlNode@ f()", asFUNCTION(XmlNodeFactory_Generic), asCALL_GENERIC); assert( r >= 0 );
    r = engine->RegisterObjectBehaviour("XmlNode", asBEHAVE_ADDREF, "void f()", asMETHODPR(ASXMLNode, AddRef, (void), void), asCALL_THISCALL); assert( r >= 0 );
    r = engine->RegisterObjectBehaviour("XmlNode", asBEHAVE_RELEASE, "void f()", asMETHODPR(ASXMLNode, Release, (void), void), asCALL_THISCALL);assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "XmlNodeType type", asOFFSET(ASXMLNode, type)); assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "string name", asOFFSET(ASXMLNode, name)); assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "dictionary& attributes", asOFFSET(ASXMLNode, attributes)); assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "array<XmlNode@>& childNodes", asOFFSET(ASXMLNode, children)); assert( r >= 0 );

    // XML functions
    r = engine->RegisterGlobalFunction("XmlNode@ XmlParseFile(const string& file)", asFUNCTIONPR(ASXMLParseFile, (const std::string&), ASXMLNode*), asCALL_CDECL); assert( r >= 0 );
    r = engine->RegisterGlobalFunction("XmlNode@ XmlParse(const string& str)", asFUNCTIONPR(ASXMLParse, (const std::string&), ASXMLNode*), asCALL_CDECL); assert( r >= 0 );
    r = engine->RegisterGlobalFunction("bool XmlWriteFile(const XmlNode& in xml,const string& file,bool sortAttributes=false)", asFUNCTIONPR(ASXMLWriteFile, (const ASXMLNode& node,const std::string&,bool), bool), asCALL_CDECL); assert( r >= 0 );
    r = engine->RegisterGlobalFunction("bool XmlWrite(const XmlNode& in xml,string& out str,bool sortAttributes=false)", asFUNCTIONPR(ASXMLWrite, (ASXMLNode& node,std::string&,bool), bool), asCALL_CDECL); assert( r >= 0 );
}

static void RegisterScriptXML_Generic(asIScriptEngine *engine)
{
    int r=0;
    // XMLNodeType enum
    r = engine->RegisterEnum("XmlNodeType"); assert(r>=0);
    r = engine->RegisterEnumValue("XmlNodeType", "kXmlElement", kXmlElement);assert(r>=0);
    r = engine->RegisterEnumValue("XmlNodeType", "kXmlComment", kXmlComment);assert(r>=0);
    r = engine->RegisterEnumValue("XmlNodeType", "kXmlText", kXmlText);assert(r>=0);

    // XMLNode class
    r = engine->RegisterObjectType("XmlNode",sizeof(ASXMLNode), asOBJ_REF); assert( r >= 0 );
    r = engine->RegisterObjectBehaviour("XmlNode", asBEHAVE_FACTORY, "XmlNode@ f()", asFUNCTION(XmlNodeFactory_Generic), asCALL_GENERIC); assert( r >= 0 );
    r = engine->RegisterObjectBehaviour("XmlNode", asBEHAVE_ADDREF, "void f()", WRAP_MFN_PR(ASXMLNode, AddRef, (void), void), asCALL_GENERIC); assert( r >= 0 );
    r = engine->RegisterObjectBehaviour("XmlNode", asBEHAVE_RELEASE, "void f()", WRAP_MFN_PR(ASXMLNode, Release, (void), void), asCALL_GENERIC);assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "XmlNodeType type", asOFFSET(ASXMLNode, type)); assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "string name", asOFFSET(ASXMLNode, name)); assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "dictionary& attributes", asOFFSET(ASXMLNode, attributes)); assert( r >= 0 );
    r = engine->RegisterObjectProperty("XmlNode", "array<XmlNode@>& childNodes", asOFFSET(ASXMLNode, children)); assert( r >= 0 );

    // XML functions
    r = engine->RegisterGlobalFunction("XmlNode@ XmlParseFile(const string& file)", WRAP_FN_PR(ASXMLParseFile, (const std::string&), ASXMLNode*), asCALL_GENERIC); assert( r >= 0 );
    r = engine->RegisterGlobalFunction("XmlNode@ XmlParse(const string& str)", WRAP_FN_PR(ASXMLParse, (const std::string&), ASXMLNode*), asCALL_GENERIC); assert( r >= 0 );
    r = engine->RegisterGlobalFunction("bool XmlWriteFile(const XmlNode& in xml,const string& file,bool sortAttributes=false)", WRAP_FN_PR(ASXMLWriteFile, (const ASXMLNode& node,const std::string&,bool), bool), asCALL_GENERIC); assert( r >= 0 );
    r = engine->RegisterGlobalFunction("bool XmlWrite(const XmlNode& in xml,string& out str,bool sortAttributes=false)", WRAP_FN_PR(ASXMLWrite, (ASXMLNode& node,std::string&,bool), bool), asCALL_GENERIC); assert( r >= 0 );
}

void RegisterScriptXML(asIScriptEngine * engine)
{
    if (strstr(asGetLibraryOptions(), "AS_MAX_PORTABILITY"))
        RegisterScriptXML_Generic(engine);
    else
        RegisterScriptXML_Native(engine);
}
END_AS_NAMESPACE




