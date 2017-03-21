#ifndef GENERATOR_H
#define GENERATOR_H

#ifndef ANGELSCRIPT_H 
// Avoid having to inform include path if header is already include before
#include <angelscript.h>
#endif

BEGIN_AS_NAMESPACE

#include "../scriptany/scriptany.h"

class CScriptDictionary;
/** A simple generator add-on for Angelscript to manage
*   coroutines like in javascript, using yield() and next() statements.
*   status: WIP.
*/
class CGenerator
{
public:
    CGenerator(asIScriptContext *context);
    ~CGenerator();

    // Memory management
    int AddRef() const;
    int Release() const;

    // Switch the execution to the next step in the generator
    // and optionally sets a return value for yield
    // Returns true if generator not done yet
    bool Next();
    bool Next(void *ref, int refTypeId);
    bool Next(asINT64 &value);
    bool Next(double &value);

    // Abort 
    //void Abort();
    const CScriptAny* GetValue()const;
    CScriptAny* GetValue();

    // creates and stores a new value for next yield return
    CScriptAny* NewYieldReturnPtr(asIScriptEngine* engine);
protected:
    bool        DoNext();

    // our reference counter for the generator object
    mutable int refCount;

    // Statistics for Garbage Collection
    asUINT   m_numExecutions;
    asUINT   m_numGCObjectsCreated;
    asUINT   m_numGCObjectsDestroyed;

    // the context associated with the generator object
    asIScriptContext * ctx;
    // the value for the next yield return (sent from caller to callee)
    CScriptAny* yieldReturn;
    // the value associated with the generator (sent back to caller)
    CScriptAny* value;
};


// Registers the following:
//
//  funcdef void generator(dictionary@)
//  void generator@ createGenerator(generatorFunc @func, dictionary @args)
//  void yield()
void RegisterGeneratorSupport(asIScriptEngine *engine);

END_AS_NAMESPACE

#endif
