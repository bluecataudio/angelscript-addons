#include <assert.h>
#include <string>

#include "generator.h"

using namespace std;

BEGIN_AS_NAMESPACE

static const asPWORD YIELD_IS_ALLOWED = 6666;
static const asPWORD YIELD_GENERATOR=6667;

static const int kYieldAllowedMagic = 321321321;

asIScriptContext * CreateContextForGenerator(asIScriptContext *currCtx, asIScriptFunction *func)
{
    asIScriptEngine *engine = currCtx->GetEngine();
    asIScriptContext *coctx = engine->RequestContext();
    if (coctx == 0)
    {
        return 0;
    }

    // Prepare the context
    int r = coctx->Prepare(func);
    if (r < 0)
    {
        // Couldn't prepare the context
        engine->ReturnContext(coctx);
        return 0;
    }
    coctx->SetUserData((void*)&kYieldAllowedMagic, YIELD_IS_ALLOWED);
    return coctx;
}

void* ScriptYieldObject(void *ref, int refTypeId)
{
    void* returnPointer=0;
    // Get a pointer to the context that is currently being executed
    asIScriptContext *ctx = asGetActiveContext();
    if (ctx)
    {
        void* data = ctx->GetUserData(YIELD_IS_ALLOWED);
        if (data != NULL && (*reinterpret_cast<int*>(data) == kYieldAllowedMagic))
        {
            // get the generator
            CGenerator* theGenerator = reinterpret_cast<CGenerator*>(ctx->GetUserData(YIELD_GENERATOR));
            if (theGenerator)
            {
                // store value
                theGenerator->GetValue()->Store(ref,refTypeId);

                // store return object - the trick is that the execution of the VM will be stopped AFTER we have returned
                // so the return value has to be a valid object, always.
                returnPointer = reinterpret_cast<void*>(theGenerator->NewYieldReturnPtr(ctx->GetEngine()));
            }
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
    return returnPointer;
}

void* ScriptYield()
{
    return ScriptYieldObject(NULL,0);
}

void* ScriptYieldInt(asINT64 &value)
{
    return	ScriptYieldObject(&value, asTYPEID_INT64);
}

void* ScriptYieldDouble(double &value)
{
    return	ScriptYieldObject(&value, asTYPEID_DOUBLE);
}

CGenerator* ScriptCreateGenerator(asIScriptFunction *func, CScriptDictionary *arg)
{
    CGenerator* gen = 0;
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
            gen = new CGenerator(coctx);
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

CGenerator::CGenerator(asIScriptContext *context) :
    ctx(context),
    refCount(1),
    yieldReturn(NULL),
    value(NULL)
{
    // store the context and create our ScriptAny value
    if (ctx)
    {
        ctx->SetUserData(this, YIELD_GENERATOR);
        value=new CScriptAny(ctx->GetEngine());
    }
    m_numExecutions = 0;
    m_numGCObjectsCreated = 0;
    m_numGCObjectsDestroyed = 0;
}

CGenerator::~CGenerator()
{
    // cleanup context
    if (ctx)
    {
        // Return the context to the engine (and possible context pool configured in it)
        ctx->SetUserData(NULL, YIELD_IS_ALLOWED);
        ctx->GetEngine()->ReturnContext(ctx);
    }
    // cleanup value
    if (value)
    {
        value->Release();
        value=NULL;
    }
    // cleanup yieldReturn cache if any
    if (yieldReturn)
    {
        yieldReturn->Release();
        yieldReturn=NULL;
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
    if (asAtomicDec(refCount) == 0)
    {
        // Delete this object as no more references to it exists
        delete this;
        return 0;
    }
    return refCount;
}

bool CGenerator::DoNext()
{
    if (ctx)
    {
        asIScriptEngine* engine = ctx->GetEngine();
        if (engine)
        {
            // Gather some statistics from the GC
            asUINT gcSize1, gcSize2, gcSize3;
            engine->GetGCStatistics(&gcSize1);

            // store pointer to previous return value container to cleanup, AFTER the call
            CScriptAny* prevYieldReturn=yieldReturn;
            yieldReturn=NULL;

            // Execute the script for this generator, until yield is called.
            int r = ctx->Execute();

            // cleanup our previous yield return object if any
            if (prevYieldReturn != NULL)
            {
                prevYieldReturn->Release();
                prevYieldReturn=NULL;
            }

            // Determine how many new objects were created in the GC
            engine->GetGCStatistics(&gcSize2);
            m_numGCObjectsCreated += gcSize2 - gcSize1;
            m_numExecutions++;

            if (r != asEXECUTION_SUSPENDED)
            {
                // The context has terminated execution (for one reason or other)
                // return the context to the pool now
                ctx->SetUserData(NULL, YIELD_IS_ALLOWED);
                ctx->SetUserData(NULL,YIELD_GENERATOR);
                value->Store(0,0);
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

bool CGenerator::Next()
{
    // returned value is empty
    if (yieldReturn != NULL)
        yieldReturn->Store(0,0);
    return DoNext();
}

bool CGenerator::Next(void *ref, int refTypeId)
{
    // store returned value inot the returned any object
    if (yieldReturn != NULL)
        yieldReturn->Store(ref,refTypeId);
    return DoNext();
}

bool CGenerator::Next(asINT64 &value)
{
    return	Next(&value, asTYPEID_INT64);
}

bool CGenerator::Next(double &value)
{
    return	Next(&value, asTYPEID_DOUBLE);
}

const CScriptAny* CGenerator::GetValue()const
{
    return value;
}

CScriptAny* CGenerator::GetValue()
{
    return value;
}

CScriptAny* CGenerator::NewYieldReturnPtr(asIScriptEngine* engine)
{
    // create new value, and store it for later (will have to be cleaned after Next is called)
    yieldReturn=new CScriptAny(engine);
    yieldReturn->AddRef();
    return yieldReturn;
}

void RegisterGeneratorSupport(asIScriptEngine *engine)
{
    int r;

    // The dictionary and any add-ons must have been registered already
    assert(engine->GetTypeInfoByDecl("dictionary"));
    assert(engine->GetTypeInfoByDecl("any"));

#ifndef AS_MAX_PORTABILITY
    // register generator object
    r = engine->RegisterObjectType("generator", sizeof(CGenerator), asOBJ_REF); assert(r >= 0);
    r = engine->RegisterObjectBehaviour("generator", asBEHAVE_ADDREF, "void f()", asMETHOD(CGenerator, AddRef), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectBehaviour("generator", asBEHAVE_RELEASE, "void f()", asMETHOD(CGenerator, Release), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectMethod("generator", "bool next()", asMETHODPR(CGenerator, Next, (void), bool), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectMethod("generator", "bool next(?&in)", asMETHODPR(CGenerator, Next, (void*,int), bool), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectMethod("generator", "bool next(const int64&in)", asMETHODPR(CGenerator, Next, (asINT64&), bool), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectMethod("generator", "bool next(const double&in)", asMETHODPR(CGenerator, Next, (double&), bool), asCALL_THISCALL); assert(r >= 0);
    r = engine->RegisterObjectMethod("generator", "any& get_value() const", asMETHODPR(CGenerator, GetValue,(void)const,const CScriptAny*), asCALL_THISCALL); assert( r >= 0 );

    // register the associated global functions and types
    r = engine->RegisterGlobalFunction("any@ yield()", asFUNCTION(ScriptYield), asCALL_CDECL); assert(r >= 0);
    r = engine->RegisterGlobalFunction("any@ yield(?&in)", asFUNCTION(ScriptYieldObject), asCALL_CDECL); assert(r >= 0);
    r = engine->RegisterGlobalFunction("any@ yield(const int64&in)", asFUNCTION(ScriptYieldInt), asCALL_CDECL); assert(r >= 0);
    r = engine->RegisterGlobalFunction("any@ yield(const double&in)", asFUNCTION(ScriptYieldDouble), asCALL_CDECL); assert(r >= 0);
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
