#ifndef GENERATOR_H
#define GENERATOR_H

#ifndef ANGELSCRIPT_H 
// Avoid having to inform include path if header is already include before
#include <angelscript.h>
#endif

BEGIN_AS_NAMESPACE

class CScriptDictionary;

// The internal structure for holding contexts
struct SContextInfo;

// The signature of the get time callback function
typedef asUINT (*TIMEFUNC_t)();

class CGenerator
{
public:
	CGenerator(asIScriptContext *context);
	~CGenerator();

    // Memory management
    int AddRef() const;
    int Release() const;

	// Switch the execution to the next step in the generator
	// Returns true if generator not done yet
	bool Next();

	// Abort 
	//void Abort();

protected:
    asIScriptContext * ctx;

	// Statistics for Garbage Collection
	asUINT   m_numExecutions;
	asUINT   m_numGCObjectsCreated;
	asUINT   m_numGCObjectsDestroyed;

    mutable int refCount;
};


// Registers the following:
//
//  funcdef void generator(dictionary@)
//  void createGenerator(generator @func, dictionary @args)
//  void yield()
void RegisterGeneratorSupport(asIScriptEngine *engine);

END_AS_NAMESPACE

#endif
