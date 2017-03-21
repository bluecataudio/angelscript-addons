#include <assert.h>
#include <string>

#include "generator.h"

using namespace std;

BEGIN_AS_NAMESPACE

static const asPWORD YIELD_IS_ALLOWED = 1002;
static const int kYieldAllowedMagic=321321321;

asIScriptContext * CreateContextForGenerator(asIScriptContext *currCtx, asIScriptFunction *func)
{
    asIScriptEngine *engine = currCtx->GetEngine();
    asIScriptContext *coctx = engine->RequestContext();
    if( coctx == 0 )
    {
        return 0;
    }

    // Prepare the context
    int r = coctx->Prepare(func);
    if( r < 0 )
    {
        // Couldn't prepare the context
        engine->ReturnContext(coctx);
        return 0;
    }
    coctx->SetUserData((void*)&kYieldAllowedMagic,YIELD_IS_ALLOWED);
    return coctx;
}

static void ScriptYield()
{
    // Get a pointer to the context that is currently being executed
    asIScriptContext *ctx = asGetActiveContext();
    if (ctx)
    {
        void* data=ctx->GetUserData(YIELD_IS_ALLOWED);
        if (data != NULL && (*reinterpret_cast<int*>(data) == kYieldAllowedMagic))
        {
            // The current context must be suspended so that VM will return from
            // the Next() method where the context manager will continue.
            ctx->Suspend();
        }
        else
        {
            // throw error
            ctx->SetException("Cannot call yield outside of a generator function");
        }
    }
}

CGenerator* ScriptCreateGenerator(asIScriptFunction *func, CScriptDictionary *arg)
{
    CGenerator* gen=0;
    if (func != 0)
    {
        asIScriptContext *ctx = asGetActiveContext();
        if (ctx)
        {
            // Create a new context for the generator
            asIScriptContext *coctx = CreateContextForGenerator(ctx, func);

            // Pass the argument to the context
            coctx->SetArgObject(0, arg);

            // The generator will call Execute() on this context when "Next" is called
            gen=new CGenerator(coctx);
        }
    }
    return gen;
}

#ifdef AS_MAX_PORTABILITY
void ScriptYield_generic(asIScriptGeneric *)
{
	ScriptYield();
}

void ScriptCreateGenerator_generic(asIScriptGeneric *gen)
{
	asIScriptFunction *func = reinterpret_cast<asIScriptFunction*>(gen->GetArgAddress(0));
	CScriptDictionary *dict = reinterpret_cast<CScriptDictionary*>(gen->GetArgAddress(1));
    ScriptCreateGenerator(func, dict);
}
#endif

CGenerator::CGenerator(asIScriptContext *context):
    ctx(context),
    refCount(1)
{
	m_numExecutions         = 0;
	m_numGCObjectsCreated   = 0;
	m_numGCObjectsDestroyed = 0;
}

CGenerator::~CGenerator()
{
    // cleanup context
    if (ctx)
    {
        // Return the context to the engine (and possible context pool configured in it)
        ctx->SetUserData(NULL,YIELD_IS_ALLOWED);
        ctx->GetEngine()->ReturnContext(ctx);
    }
}

int CGenerator::AddRef() const
{
    // Increase counter
    return asAtomicInc(refCount);
}

int CGenerator::Release() const
{
    // Decrease the ref counter
    if( asAtomicDec(refCount) == 0 )
    {
        // Delete this object as no more references to it exists
        delete this;
        return 0;
    }
    return refCount;
}

bool CGenerator::Next()
{
    if (ctx)
    {
        asIScriptEngine* engine=ctx->GetEngine();
        if (engine)
        {
            // Gather some statistics from the GC
            asUINT gcSize1, gcSize2, gcSize3;
            engine->GetGCStatistics(&gcSize1);

            // Execute the script for this generator, until yield is called.
            int r = ctx->Execute();

            // Determine how many new objects were created in the GC
            engine->GetGCStatistics(&gcSize2);
            m_numGCObjectsCreated += gcSize2 - gcSize1;
            m_numExecutions++;

            if (r != asEXECUTION_SUSPENDED)
            {
                // The context has terminated execution (for one reason or other)
                // return the context to the pool now
                ctx->SetUserData(NULL,YIELD_IS_ALLOWED);
                engine->ReturnContext(ctx);
                ctx = NULL;
            }

            // Destroy all known garbage if any new objects were created
            if (gcSize2 > gcSize1)
            {
                engine->GarbageCollect(asGC_FULL_CYCLE | asGC_DESTROY_GARBAGE);

                // Determine how many objects were destroyed
                engine->GetGCStatistics(&gcSize3);
                m_numGCObjectsDestroyed += gcSize3 - gcSize2;
            }
            // Just run an incremental step for detecting cyclic references
            engine->GarbageCollect(asGC_ONE_STEP | asGC_DETECT_GARBAGE);
        }
    }

    return ctx != NULL;
}

void RegisterGeneratorSupport(asIScriptEngine *engine)
{
	int r; 

    // The dictionary add-on must have been registered already
    assert(engine->GetTypeInfoByDecl("dictionary"));

#ifndef AS_MAX_PORTABILITY
    // register generator object
    r = engine->RegisterObjectType("generator", sizeof(CGenerator), asOBJ_REF); assert(r >= 0);
    r = engine->RegisterObjectBehaviour("generator", asBEHAVE_ADDREF, "void f()", asMETHOD(CGenerator, AddRef), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectBehaviour("generator", asBEHAVE_RELEASE, "void f()", asMETHOD(CGenerator, Release), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectMethod("generator", "bool next()", asMETHODPR(CGenerator, Next, (void), bool), asCALL_THISCALL); assert(r >= 0);

    // register the associated global functions and types
    r = engine->RegisterGlobalFunction("void yield()", asFUNCTION(ScriptYield), asCALL_CDECL); assert(r >= 0);
    r = engine->RegisterFuncdef("void generatorFunc(dictionary@)");
    r = engine->RegisterGlobalFunction("generator@ createGenerator(generatorFunc @+, dictionary @+)", asFUNCTION(ScriptCreateGenerator), asCALL_CDECL); assert(r >= 0);
#else
    // not implemented yet
    ASSERT(0);
    r = engine->RegisterGlobalFunction("void yield()", asFUNCTION(ScriptYield_generic), asCALL_GENERIC); assert(r >= 0);
    r = engine->RegisterFuncdef("void coroutine(dictionary@)");
    r = engine->RegisterGlobalFunction("void createCoRoutine(coroutine @, dictionary @)", asFUNCTION(ScriptCreateCoRoutine_generic), asCALL_GENERIC); assert(r >= 0);
#endif
}

END_AS_NAMESPACE
