> 🌐 **언어 (Language):** [English](README.md) | **한국어**

## 소개 (About)

**metal-cpp**는 C++로 작성된 그래픽스 애플리케이션(예: 게임 엔진)에 Metal 기능을 손쉽게 추가할 수 있도록 돕는 저오버헤드(low-overhead) 헤더 전용(header-only) Metal C++ 인터페이스입니다. **metal-cpp**를 사용하면 별도의 브릿지(shim) 계층을 직접 만들 필요 없이, 기존 C++ 코드 어디에서나 Metal 함수를 직접 호출할 수 있습니다.


## 주요 특징 (Highlights)

- Metal Objective-C 헤더를 즉시 대체(Drop-in)할 수 있는 C++ 인터페이스입니다.
- 모든 Metal Objective-C 클래스, 상수, 열거형(enum)을 `MTL` C++ 네임스페이스에 1:1로 직접 매핑합니다.
- C++ 함수 호출의 인라인화(inlining)를 통해 Metal Objective-C 헤더를 직접 호출할 때와 비교해 측정 가능한 수준의 오버헤드가 전혀 없습니다.
- 추가적인 메모리 할당을 유발하는 래퍼 컨테이너를 사용하지 않습니다.
- `NS::Object`에서 `constexpr`를 사용하므로 C++17 이상이 필요합니다.
- iOS, macOS, tvOS 전반에 걸쳐 동일한 헤더 파일과 함수/상수/열거형 가용성을 제공합니다.
- 하위 호환성 보장: 모든 `bool MTL::Device::supports...()` 함수는 필요한 셀렉터(selector)가 런타임에 존재하는지 확인하고, 존재하지 않을 경우 자동으로 `false`를 반환합니다.
- 문자열(`ErrorDomain`) 상수는 약한 연결(weak-linked)로 처리되며, 사용할 수 없는 환경에서는 자동으로 `nullptr`로 설정됩니다.

## 변경 이력 (Changelog)

| 버전 | 변경 사항 |
|-|-|
| macOS 15, iOS 18 | macOS 15 및 iOS 18의 모든 Metal API 추가. |
| macOS 14, iOS 17 | **MetalFX** 프레임워크 지원 추가. <br/>macOS 14 및 iOS 17의 모든 API 추가. |
| macOS 13.3, iOS 16.4 | macOS 13.3 및 iOS 16.4의 모든 API 추가. |
| macOS 13, iOS 16 | macOS 13 및 iOS 16의 모든 API 추가.<br />메모리 관리를 돕는 선택적 `NS::SharedPtr<T>` 타입 신규 도입.<br/>`CA::MetalLayer`를 생성하는 편의 함수 추가.<br/>리터럴로부터 문자열을 더 빠르게 생성할 수 있는 `MTLSTR(str)` 매크로 추가.<br/>포인터 배열을 입력으로 받는 함수들의 시그니처 문제 수정.<br/>`MTL::LinkedFunctions`의 `setGroups()` 함수 시그니처 문제 수정.|
| macOS 12, iOS 15 | 최초 릴리즈. |

## 메모리 할당 정책 (Memory Allocation Policy)

**metal-cpp**는 Cocoa, Cocoa Touch 및 CoreFoundation의 객체 할당 정책을 따릅니다. C++ 객체는 자동 참조 카운팅(ARC, Automatic Reference Counting) 대상이 아니므로, **metal-cpp**를 사용할 때는 이러한 규칙을 정확히 이해하는 것이 특히 중요합니다.

**metal-cpp** 객체는 참조 카운팅(reference counted) 방식으로 관리됩니다. 객체 생명주기를 명확히 전달하고 관리하기 위해 다음 규칙을 준수합니다:

1. *이름이* `alloc` *,* `new` *,* `copy` *,* `mutableCopy` *, 또는* `Create` *로 시작하는 메서드가 반환한 모든 객체의 소유권은 호출자(당신)에게 있습니다*. 해당 메서드는 `retainCount`가 `1`인 상태로 객체를 반환합니다.
2. *객체의* `retain()` *메서드를 호출하여 소유권을 획득할 수 있습니다*. 전달받은 객체는 일반적으로 그 객체를 전달받은 메서드 내부에서는 유효함이 보장됩니다. `retain`은 다음 두 가지 상황에서 사용합니다: (1) 접근자 메서드(setter)를 구현하거나 객체의 소유권을 직접 가져오고자 할 때, (2) 다른 작업의 부수 효과(side-effect)로 인해 객체가 해제되는 것을 방지하고자 할 때.
3. *소유한 객체가 더 이상 필요하지 않으면 반드시 소유권을 포기해야 합니다*. 객체의 `release()` 또는 `autorelease()` 메서드를 호출하여 소유권을 포기합니다.
4. *자신이 소유하지 않은 객체의 소유권을 임의로 포기(`release`)해서는 안 됩니다*.

객체의 `retainCount`가 `0`에 도달하면 해당 객체는 즉시 메모리에서 해제(deallocated)됩니다. 이미 해제된 객체의 메서드를 호출하는 것은 불법(illegal)이며 애플리케이션 충돌(crash)을 유발할 수 있습니다.

### AutoreleasePool과 객체 (AutoreleasePools and Objects)

**metal-cpp**에서 임시 객체를 생성하는 여러 메서드는 생명주기 관리를 돕기 위해 해당 객체를 `AutoreleasePool`에 등록합니다. 이 경우 **metal-cpp**가 객체를 생성한 뒤 `AutoreleasePool`에 추가하며, 나중에 풀(pool)을 해제(`release` 또는 `drain`)할 때 등록된 객체들이 일괄 해제됩니다.

임시 객체가 `AutoreleasePool`에 추가되므로 개발자가 직접 `release()`를 명시적으로 호출할 필요가 없으며, `AutoreleasePool`이 암묵적으로 생명주기를 관리하도록 맡길 수 있습니다.

이름이 `alloc`, `new`, `copy`, `mutableCopy`, 또는 `Create`로 시작하지 않는 메서드로 객체를 생성하면, 해당 생성 메서드는 객체를 자동으로 오토릴리즈 풀(autorelease pool)에 추가합니다.

프로그램 메인 스레드에서 `AutoreleasePool`의 일반적인 유효 범위(scope)는 렌더링 1프레임입니다. 스레드가 윈도우 시스템으로부터 입력과 이벤트를 수신하는 객체인 런루프(RunLoop)로 제어권을 반환할 때 풀이 *비워지며(drained)* 내부 객체들을 해제합니다.

프로그램의 작업 메모리(working set) 크기를 줄이기 위해 더 작은 범위 단위로 추가적인 `AutoreleasePool`을 직접 생성하고 관리할 수 있으며, 프로그램이 추가로 생성하는 모든 보조 스레드에서는 반드시 직접 `AutoreleasePool`을 생성해야 합니다.

만약 객체의 생명주기를 `AutoreleasePool` 인스턴스의 범위 너머까지 연장해야 한다면, 풀이 비워지기 전에 해당 객체의 `retain()` 메서드를 호출하여 소유권을 획득할 수 있습니다. 이 경우 객체가 더 이상 필요하지 않을 때 적절한 `release()` 호출을 수행할 책임은 개발자에게 있습니다.

메모리 관리 규칙에 대한 더 자세한 설명은 다음 공식 문서를 참고하세요:
- https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/MemoryMgmt/Articles/mmRules.html
- https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFMemoryMgmt/Concepts/Ownership.html

애플리케이션의 RunLoop에 대한 자세한 내용은 다음 문서를 참고하세요:
- https://developer.apple.com/documentation/foundation/nsrunloop

### AutoreleasePool의 사용 및 디버깅 (Use and debug AutoreleasePools)

오토릴리즈(autoreleased) 객체를 생성할 때 이를 감싸는 상위 `AutoreleasePool`이 없으면 해당 객체는 메모리 누수(leak)가 발생합니다.

이를 방지하기 위해 일반적으로 프로그램의 `main` 함수와 새로 생성하는 모든 스레드의 진입 함수(entry function)에 `AutoreleasePool`을 생성합니다. 또한 렌더링 루프처럼 오토릴리즈 객체가 다수 생성되는 구간에서 프로그램의 최대 메모리 사용량(high memory watermark)이 커지는 것을 막기 위해 추가적인 `AutoreleasePool`을 생성할 수도 있습니다.

스레드에 사용 가능한 상위 `AutoreleasePool`이 없어 오토릴리즈 객체가 누수될 때 런타임 경고를 출력하려면 환경 변수 `OBJC_DEBUG_MISSING_POOLS=YES`를 설정하세요.

또한 macOS에서는 메모리 그래프(memgraph) 파일이나 프로세스 ID를 대상으로 `leaks --autoreleasePools` 명령을 실행하여 프로그램의 `AutoreleasePool` 목록과 각 풀이 포함하고 있는 모든 객체를 확인할 수 있습니다.

### NS::SharedPtr

**metal-cpp** 헤더에는 앱에서 메모리를 수동으로 관리하는 부담을 덜어주는 선택적 `NS::SharedPtr<>`(스마트 포인터) 템플릿이 포함되어 있습니다.

**metal-cpp**의 공유 포인터는 자체 메모리 모델에 특화된 최적화를 구현한다는 점에서 표준 라이브러리의 `std::shared_ptr<>`와 다릅니다. 예를 들어, **metal-cpp**의 공유 포인터는 `NS::Object` 타입에 내장된 참조 카운팅 구현을 그대로 활용하므로 표준 라이브러리 `std::shared_ptr<>`의 제어 블록 오버헤드를 피할 수 있습니다.

> **📌 참고 (Note):**
> **metal-cpp** 공유 포인터의 소멸자(destructor)는 자신이 감싸고 있는 포인터의 `release()` 메서드를 항상 호출합니다.

애플리케이션의 의도에 맞는 **metal-cpp** 팩토리 메서드를 호출하여 `NS::SharedPtr<>`를 생성할 수 있습니다:

* 포인터의 소유권을 새로운 공유 포인터 인스턴스로 **이전(transfer)**하려면 `NS::TransferPtr()` 팩토리 함수를 호출합니다. 이 함수는 대상 객체의 `retainCount`를 증가시키지 않으므로 RAII(Resource Acquisition Is Initialization) 패턴을 구현할 때 올바른 선택입니다.

* 포인터의 소유권을 다른 엔티티와 **공유(share)**하려면 `NS::RetainPtr()` 팩토리 함수를 호출합니다. 이 함수는 대상 객체에 대한 강한 참조(strong reference)를 생성하고 `retainCount`를 1 증가시키므로, `AutoreleasePool` 인스턴스의 범위 너머로 객체의 생명주기를 연장할 때도 사용할 수 있습니다.

`NS::SharedPtr<>`의 사용은 선택 사항입니다.

### nullptr

Objective-C와 마찬가지로, `nullptr` "객체"에 대해 `retain()` 및 `release()`를 포함한 모든 메서드를 호출하는 것은 문법적·런타임적으로 안전(legal)합니다. `nullptr`에 대한 메서드 호출이 함수 호출 자체의 미세한 오버헤드를 수반하긴 하지만, 실질적인 동작 결과는 아무 일도 하지 않는 NOP(No Operation)와 같습니다.

반대로, 어떤 포인터에 대해 메서드를 호출했을 때 크래시가 나지 않았다고 해서 그 포인터가 가리키는 객체가 유효한 객체라고 단정해서는 안 됩니다.

## 프로젝트에 metal-cpp 추가하기 (Adding metal-cpp to a Project)

단순히 `Metal/Metal.hpp`를 `#include` 하면 됩니다. 단, 셀렉터(selector)와 클래스 심볼이 정상적으로 링크되도록 프로젝트의 `.cpp` 파일 중 **단 한 곳**에 다음 매크로 정의를 추가해야 합니다:

```cpp
#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "Metal/Metal.hpp"
```

QuartzCore 래퍼(`CAMetalLayer` 등)도 함께 사용하려면 다음을 추가합니다:

```cpp
#define CA_PRIVATE_IMPLEMENTATION

#include "QuartzCore/QuartzCore.hpp"
```

## 단일 헤더 파일 생성하기 (Generating a Single Header File)

순전히 선택 사항입니다. 다음 스크립트를 실행하여 모든 **metal-cpp** 헤더를 하나로 합친 단일 헤더 파일을 생성할 수 있습니다:

```shell
./SingleHeader/MakeSingleHeader.py Foundation/Foundation.hpp QuartzCore/QuartzCore.hpp Metal/Metal.hpp MetalFX/MetalFX.hpp
```

기본적으로 생성 스크립트는 결과물을 `./SingleHeader/Metal.hpp`에 기록합니다. 출력 파일 이름을 변경하려면 `-o` 옵션을 사용하세요.

## 전역 심볼 가시성 (Global Symbol Visibility)

**metal-cpp**는 기본적으로 모든 심볼을 `default` 가시성으로 표시합니다. 이 동작을 재정의하고 심볼을 외부에서 숨기려면 `METALCPP_SYMBOL_VISIBILITY_HIDDEN` 매크로를 정의하세요.

## 사용 예제 (Examples)

#### 디바이스(Device) 생성하기

###### Objective-C (ARC 사용 시)

```objc
id< MTLDevice > device = MTLCreateSystemDefaultDevice();

// ...
```

###### Objective-C (수동 참조 카운팅)

```objc
id< MTLDevice > device = MTLCreateSystemDefaultDevice();

// ...

[device release];
```

###### C++

```cpp
MTL::Device* pDevice = MTL::CreateSystemDefaultDevice();

// ...

pDevice->release();
```

###### C++ (NS::SharedPtr 사용 시)

```cpp
NS::SharedPtr< MTL::Device > pDevice = NS::TransferPtr( MTL::CreateSystemDefaultDevice() );

// ...
```

#### C++로 직접 매핑되는 Metal 함수 호출

###### Objective-C (ARC 사용 시)

```objc
MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];

[samplerDescriptor setSAddressMode: MTLSamplerAddressModeRepeat];
[samplerDescriptor setTAddressMode: MTLSamplerAddressModeRepeat];
[samplerDescriptor setRAddressMode: MTLSamplerAddressModeRepeat];
[samplerDescriptor setMagFilter: MTLSamplerMinMagFilterLinear];
[samplerDescriptor setMinFilter: MTLSamplerMinMagFilterLinear];
[samplerDescriptor setMipFilter: MTLSamplerMipFilterLinear];
[samplerDescriptor setSupportArgumentBuffers: YES];

id< MTLSamplerState > samplerState = [device newSamplerStateWithDescriptor:samplerDescriptor];
```

###### Objective-C (수동 참조 카운팅)

```objc
MTLSamplerDescriptor* samplerDescriptor = [[MTLSamplerDescriptor alloc] init];

[samplerDescriptor setSAddressMode: MTLSamplerAddressModeRepeat];
[samplerDescriptor setTAddressMode: MTLSamplerAddressModeRepeat];
[samplerDescriptor setRAddressMode: MTLSamplerAddressModeRepeat];
[samplerDescriptor setMagFilter: MTLSamplerMinMagFilterLinear];
[samplerDescriptor setMinFilter: MTLSamplerMinMagFilterLinear];
[samplerDescriptor setMipFilter: MTLSamplerMipFilterLinear];
[samplerDescriptor setSupportArgumentBuffers: YES];

id< MTLSamplerState > samplerState = [device newSamplerStateWithDescriptor:samplerDescriptor];

[samplerDescriptor release];

// ...

[samplerState release];
```

###### C++

```cpp
MTL::SamplerDescriptor* pSamplerDescriptor = MTL::SamplerDescriptor::alloc()->init();

pSamplerDescriptor->setSAddressMode( MTL::SamplerAddressModeRepeat );
pSamplerDescriptor->setTAddressMode( MTL::SamplerAddressModeRepeat );
pSamplerDescriptor->setRAddressMode( MTL::SamplerAddressModeRepeat );
pSamplerDescriptor->setMagFilter( MTL::SamplerMinMagFilterLinear );
pSamplerDescriptor->setMinFilter( MTL::SamplerMinMagFilterLinear );
pSamplerDescriptor->setMipFilter( MTL::SamplerMipFilterLinear );
pSamplerDescriptor->setSupportArgumentBuffers( true );

MTL::SamplerState* pSamplerState = pDevice->newSamplerState( pSamplerDescriptor );

pSamplerDescriptor->release();

// ...

pSamplerState->release();
```

###### C++ (NS::SharedPtr 사용 시)

```cpp
NS::SharedPtr< MTL::SamplerDescriptor > pSamplerDescriptor = NS::TransferPtr( MTL::SamplerDescriptor::alloc()->init() );

pSamplerDescriptor->setSAddressMode( MTL::SamplerAddressModeRepeat );
pSamplerDescriptor->setTAddressMode( MTL::SamplerAddressModeRepeat );
pSamplerDescriptor->setRAddressMode( MTL::SamplerAddressModeRepeat );
pSamplerDescriptor->setMagFilter( MTL::SamplerMinMagFilterLinear );
pSamplerDescriptor->setMinFilter( MTL::SamplerMinMagFilterLinear );
pSamplerDescriptor->setMipFilter( MTL::SamplerMipFilterLinear );
pSamplerDescriptor->setSupportArgumentBuffers( true );

NS::SharedPtr< MTL::SamplerState > pSamplerState = NS::TransferPtr( pDevice->newSamplerState( pSamplerDescriptor.get() ) );
```

#### 매끄러운 통합을 위해 제공되는 Foundation 클래스 바인딩

###### Objective-C (ARC 사용 시)

```objc
NSAutoreleasePool*  pool   = [[NSAutoreleasePool alloc] init];
NSString*           string = [NSString stringWithCString: "Hello World" encoding: NSASCIIStringEncoding];

printf( "string = \"%s\"\n", [string cStringUsingEncoding: NSASCIIStringEncoding] );
```

###### Objective-C (수동 참조 카운팅)

```objc
NSAutoreleasePool* pool   = [[NSAutoreleasePool alloc] init];
NSString*          string = [NSString stringWithCString: "Hello World" encoding: NSASCIIStringEncoding];
								
printf( "string = \"%s\"\n", [string cStringUsingEncoding: NSASCIIStringEncoding] );

[pool release];
```

###### C++

```cpp
NS::AutoreleasePool* pPool   = NS::AutoreleasePool::alloc()->init();
NS::String*          pString = NS::String::string( "Hello World", NS::ASCIIStringEncoding );

printf( "pString = \"%s\"\n", pString->cString( NS::ASCIIStringEncoding ) );

pPool->release();
```

###### C++ (NS::SharedPtr 사용 시)

```cpp
NS::SharedPtr< NS::AutoreleasePool > pPool   = NS::TransferPtr( NS::AutoreleasePool::alloc()->init() );
NS::String*                          pString = NS::String::string( "Hello World", NS::ASCIIStringEncoding );

printf( "pString = \"%s\"\n", pString->cString( NS::ASCIIStringEncoding ) );
```

#### 컨테이너 (Containers)

`NS::Array` 및 `NS::Dictionary` 인스턴스를 생성하려면 CoreFoundation 프레임워크를 사용합니다.

```cpp
MTL::AccelerationStructureTriangleGeometryDescriptor* pGeoDescriptor  = MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();
CFTypeRef                                             descriptors[]   = { ( CFTypeRef )( pGeoDescriptor ) };
NS::Array*                                            pGeoDescriptors = ( NS::Array* )( CFArrayCreate( kCFAllocatorDefault, descriptors, SIZEOF_ARRAY( descriptors), &kCFTypeArrayCallBacks ) );

// ...

pGeoDescriptors->release();
```

`NS::Array` 및 `NS::Dictionary`와 같은 컨테이너는 내부에 보유한 객체들을 `retain`하며, 컨테이너가 해제될 때 내부 객체들을 함께 `release`합니다.

#### Metal Drawable에 접근하기 (Accessing the Metal Drawable)

```cpp
#include <QuartzCore/QuartzCore.hpp>

// ...

CA::MetalLayer*    pMetalLayer    = /* 뷰와 연결된 레이어 */;
CA::MetalDrawable* pMetalDrawable = pMetalLayer->nextDrawable();

// ...
```
