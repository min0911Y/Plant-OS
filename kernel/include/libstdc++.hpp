// #ifndef __STRING__HPP__
// #define __STRING__HPP__
// class string {
// public:
//   char *str;
//   int   len;
//   string();
//   string(char *s);
//   string(string &s);
//   string(string &&s);
//   string(const char *s);
//   ~string();
//   const char *c_str();
//   string     &operator=(string &s);
//   string     &operator=(char *s);
//   string     &operator+=(string &s);
//   string     &operator+=(char *s);
//   string     &operator+=(i8 c);
//   string     &operator+=(i16 s);
//   string     &operator+=(i32 i);
//   string     &operator+=(i64 l);
//   string     &operator+=(u8 c);
//   string     &operator+=(u16 s);
//   string     &operator+=(u32 i);
//   string     &operator+=(u64 l);
//   // string& operator+=(void* p);
//   // string& operator+=(char c);
//   bool        operator==(string &s);
//   bool        operator==(char *s);
//   bool        operator!=(string &s);
//   bool        operator!=(char *s);
//   bool        operator<(string &s);
//   bool        operator<(char *s);
//   bool        operator>(string &s);
//   bool        operator>(char *s);
//   bool        operator<=(string &s);
//   bool        operator<=(char *s);
//   bool        operator>=(string &s);
//   bool        operator>=(char *s);
//   char        operator[](u32 i);
//   string      operator+(string &s);
//   string      operator+(char *s);
//   string      operator+(char c);
//   string      operator+(int i);
//   string      operator+(long l);
//   string      operator+(u32 i);
//   string      operator+(u64 l);
//   string      operator+(u16 s);
//   string      operator+(u8 c);
//   string      operator+(const char *s);
//   size_t      length();
// };
// class COUT {
// public:
//   COUT &operator<<(char *s);
//   COUT &operator<<(const char *s);
//   COUT &operator<<(char c);
//   COUT &operator<<(int i);
//   COUT &operator<<(long l);
//   COUT &operator<<(u32 i);
//   COUT &operator<<(u32 l);
//   COUT &operator<<(u16 s);
//   COUT &operator<<(u8 c);
//   COUT &operator<<(string &s);
// };
// static COUT cout;
// #define endl "\n"
// #endif