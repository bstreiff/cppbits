// This is an attempt to replicate the C# [Flags] attribute in C++ in order to get
// bitwise operations on types while still maintaining some amount of type-safety.
//
// Tested with MSVC 12.0.
//
// To use:
//
//    Declare an enum or enum class, optionally using a width specifier.
//    It is recommended that you declare the enumeration constants in powers of two,
//    with a 'None' constant.
//
//       enum BitmaskType : unsigned int
//       {
//          kBitmaskTypeNone = 0,
//          kBitmaskTypeA = 1,
//          kBitmaskTypeB = 2,
//          kBitmaskTypeC = 4
//       };
//
//    Declare a type specialization of cppbits::is_enum_flags for your type:
//
//       namespace cppbits {
//          template<>
//          struct is_enum_flags<BitmaskType> : std::true_type{};
//       }
//
//    Enjoy! You can now use &, |, ^, ~, <<, >> with the enum type as if it were
//    an integer. Conversions from integer will require explicit casting, as
//    will mixing enum types.
//
//       good:
//          BitmaskType x = kBitmaskTypeNone;
//          x &= kBitmaskTypeA;
//          x = x | kBitmaskTypeB;
//
//       bad:
//          x = 4;
//          x = kBitmaskTypeA | kOtherBitmaskTypeB;
//
//
// ===============================================================================
// This file is released into the public domain. See LICENCE for more information.
// ===============================================================================

#include <type_traits>

// Try not to pollute the global namespace with my own type traits.
namespace cppbits {

template<class T>
struct is_enum_flags : public std::false_type
{ };

} // namespace cppbits

// Unfortunately because the enum type itself is allowed to be in any namespace,
// the operator overloads have to go into the global namespace.
template<class T>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value, T>::type
   operator~(const T a)
{
   return static_cast<T>(~static_cast<std::underlying_type<T>::type>(a));
}

template<class T>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value, T>::type
   operator|(const T a, const T b)
{
   return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) | static_cast<std::underlying_type<T>::type>(b));
}

template<class T>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value, T>::type
   operator&(const T a, const T b)
{
   return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) & static_cast<std::underlying_type<T>::type>(b));
}

template<class T>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value, T>::type
   operator^(const T a, const T b)
{
   return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) ^ static_cast<std::underlying_type<T>::type>(b));
}

template<class T, class Integer>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value && std::is_integral<Integer>::value, T>::type
   operator>>(const T a, const Integer b)
{
   return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) >> b);
}

template<class T, class Integer>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value && std::is_integral<Integer>::value, T>::type
   operator<<(const T a, const Integer b)
{
   return static_cast<T>(static_cast<std::underlying_type<T>::type>(a) << b);
}

template<class T>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value, T&>::type
   operator|=(T& a, const T b)
{
   return ((a = static_cast<T>(static_cast<std::underlying_type<T>::type>(a) | static_cast<std::underlying_type<T>::type>(b))));
}

template<class T>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value, T&>::type
   operator&=(T& a, const T b)
{
   return ((a = static_cast<T>(static_cast<std::underlying_type<T>::type>(a) & static_cast<std::underlying_type<T>::type>(b))));
}

template<class T>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value, T&>::type
   operator^=(T& a, const T b)
{
   return ((a = static_cast<T>(static_cast<std::underlying_type<T>::type>(a) ^ static_cast<std::underlying_type<T>::type>(b))));
}

template<class T, class Integer>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value && std::is_integral<Integer>::value, T&>::type
   operator>>=(T& a, const Integer b)
{
   return ((a = static_cast<T>(static_cast<std::underlying_type<T>::type>(a) >> b)));
}

template<class T, class Integer>
inline typename std::enable_if<
      std::is_enum<T>::value && cppbits::is_enum_flags<T>::value && std::is_integral<Integer>::value, T&>::type
   operator<<=(T& a, const Integer b)
{
   return ((a = static_cast<T>(static_cast<std::underlying_type<T>::type>(a) << b)));
}
