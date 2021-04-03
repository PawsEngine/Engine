#pragma once

#include "Engine/System/Definition.h"
#include "Engine/Collection/Dictionary.h"
#include "Engine/System/SharedPtr.h"
#include "Engine/System/UniquePtr.h"
#include "Engine/System/Variant.h"
#include "Engine/System/String.h"
#include "Engine/System/Object.h"

namespace Engine{
	class ReflectionClass;
	class ReflectionMethod;
	class ReflectionProperty;
	class ReflectionSignal;

	class Reflection final {
	public:
		STATIC_CLASS(Reflection);

		static bool IsClassExists(const String& name);

		static ReflectionClass* GetClass(const String& name);
		
		template<typename T>
		static ReflectionClass* AddClass() {
			SharedPtr<ReflectionClass> data = SharedPtr<ReflectionClass>::Create();

			data->name = T::GetClassNameStatic();
			data->parentName = T::GetParentClassNameStatic();

			return (GetData().Add(T::GetClassNameStatic(), data) ? data.GetRaw() : nullptr);
		}

	private:
		using ClassData = Dictionary<String, SharedPtr<ReflectionClass>>;
		static ClassData& GetData();
	};

	class ReflectionClass final {
	public:
		String GetName() const;
		String GetParentName() const;

		bool IsInstantiatable() const;
		void SetInstantiable(bool instantiable);

		UniquePtr<Object> Instantiate() const;
	private:
		friend class Reflection;

		String name;
		String parentName;
		bool instantiable = true;
		UniquePtr<Object>(*instantiator)() = nullptr;

		using MethodData = Dictionary<String, SharedPtr<ReflectionMethod>>;
	};

	class ReflectionMethod {
	public:
		virtual ~ReflectionMethod();

		enum class InvokeResult {
			OK,
			ArgumentCountNotMatching,

		};
		virtual InvokeResult Invoke(Object* target, Variant* arguments, int32 argumentCount, Variant& returnValue);

	private:
		friend class ReflectionClass;

		String name;
		Variant::Type returnType;
		int32 argumentCount;
	};
}

#pragma region Common name getter.
#define _REFLECTION_CLASS_NAME_GETTERS(name,parent)									\
static ::Engine::String GetClassNameStatic(){										\
	return STRING_LITERAL(name);													\
}																					\
virtual ::Engine::String GetClassName() const{										\
	return STRING_LITERAL(name);													\
}																					\
static ::Engine::String GetParentClassNameStatic(){									\
	return STRING_LITERAL(parent);													\
}																					\
virtual ::Engine::String GetParentClassName() const{								\
	return STRING_LITERAL(parent);													\
}
#pragma endregion

#pragma region Auto register
#define _REFLECTION_CLASS_AUTO_REGISTER()											\
	class _ReflectionInitializerCaller{												\
	public:																			\
		_ReflectionInitializerCaller(){												\
			_InitializeReflection();												\
		}																			\
	};																				\
	inline static _ReflectionInitializerCaller _reflectionInitializerCaller{}
#pragma endregion

#pragma region Initializer components

#pragma region Initializer common head
#define _REFLECTION_CLASS_INITIALIZER_HEAD()									\
	static void _InitializeReflection(){												\
		static bool inited=false;														\
		if(inited){																		\
			return;																		\
		}
#pragma endregion

#define _REFLECTION_CLASS_INITIALIZER_CALL_PARENT(parent) parent::_InitializeReflection()

#pragma region Initializer common tail
#define _REFLECTION_CLASS_INITIALIZER_TAIL(name)								\
		::Engine::ReflectionClass* ptr=::Engine::Reflection::AddClass<name>();		\
		FATAL_ASSERT_CRASH(ptr!=nullptr,"Failed to register class.");				\
		_InitializeCustomReflection(ptr);											\
																					\
		inited=true;																\
	}
#pragma endregion

#define _REFLECTION_CLASS_INITIALIZER_CUSTOM() static void _InitializeCustomReflection(::Engine::ReflectionClass* c)

#pragma endregion

#pragma region Root class register, not calling parent
#define REFLECTION_ROOTCLASS(name)													\
public:																				\
	_REFLECTION_CLASS_NAME_GETTERS(#name,"")										\
																					\
protected:																			\
	_REFLECTION_CLASS_INITIALIZER_HEAD()											\
	_REFLECTION_CLASS_INITIALIZER_TAIL(name)										\
																					\
private:																			\
	_REFLECTION_CLASS_AUTO_REGISTER();												\
	_REFLECTION_CLASS_INITIALIZER_CUSTOM()
		
#pragma endregion

#pragma region Normal class register, calling parent
// Name and ParentName must be located from the root namespace!
// e.g. Object should be Engine::Object
#define REFLECTION_CLASS(name,parent)												\
public:																				\
	_REFLECTION_CLASS_NAME_GETTERS(#name,#parent)									\
																					\
protected:																			\
	_REFLECTION_CLASS_INITIALIZER_HEAD()											\
	_REFLECTION_CLASS_INITIALIZER_CALL_PARENT(parent);								\
	_REFLECTION_CLASS_INITIALIZER_TAIL(name)										\
																					\
private:																			\
	_REFLECTION_CLASS_AUTO_REGISTER();												\
	_REFLECTION_CLASS_INITIALIZER_CUSTOM()
#pragma endregion

#define REFLECTION_CLASS_INSTANTIABLE(instantiable) c->SetInstantiable(instantiable)
