#pragma once

#include <algorithm>
#include <sstream>
#include <limits>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <cassert>

namespace argh
{
   // Terminology:
   // A command line is composed of 2 types of args:
   // 1. Positional args, i.e. free standing values
   // 2. Options: args beginning with '-'. We identify two kinds:
   //    2.1: Flags: boolean options =>  (exist ? true : false)
   //    2.2: Parameters: a name followed by a non-option value

	enum Mode { PREFER_FLAG_FOR_UNREG_OPTION = 1 << 0,
		PREFER_PARAM_FOR_UNREG_OPTION = 1 << 1,
		NO_SPLIT_ON_EQUALSIGN = 1 << 2,
		SINGLE_DASH_IS_MULTIFLAG = 1 << 3,
	};

   template<typename CharType = char>
   class parser
   {
	   using Tstring = std::basic_string<CharType, std::char_traits<CharType>, std::allocator<CharType>>;
	   using Tstring_view = std::basic_string_view<CharType>;
	   using Tistringstream = std::basic_istringstream<CharType, std::char_traits<CharType>, std::allocator<CharType>>;
	   using Tostringstream = std::basic_ostringstream<CharType, std::char_traits<CharType>, std::allocator<CharType>>;

   public:

	  parser() = default;

      parser(std::initializer_list<CharType const* const> pre_reg_names)
      {  add_params(pre_reg_names); }

      parser(const CharType* const argv[], int mode = PREFER_FLAG_FOR_UNREG_OPTION)
      {  parse(argv, mode); }

      parser(int argc, const CharType* const argv[], int mode = PREFER_FLAG_FOR_UNREG_OPTION)
      {  parse(argc, argv, mode); }

      void add_param(Tstring const& name)
	  {
		  registeredParams_.insert(trim_leading_dashes(name));
	  }

	  void add_params(std::initializer_list<CharType const* const> init_list)
	  {
		  for (auto& name : init_list)
			  registeredParams_.insert(trim_leading_dashes(name));
	  }

      void parse(const CharType* const argv[], int mode = PREFER_FLAG_FOR_UNREG_OPTION)
	  {
		  int argc = 0;
		  for (auto argvp = argv; *argvp; ++argc, ++argvp);
		  parse(argc, argv, mode);
	  }

	  void parse(int argc, const CharType* const argv[], int mode = PREFER_FLAG_FOR_UNREG_OPTION)
	  {
		  parse(static_cast<size_t>(argc), argv, mode);
	  }

	  void parse(size_t argc, const CharType* const argv[], int mode = PREFER_FLAG_FOR_UNREG_OPTION);

      std::multiset<Tstring>        const& flags()    const { return flags_;    }
      std::map<Tstring, Tstring>    const& params()   const { return params_;   }
      std::vector<Tstring>          const& pos_args() const { return pos_args_; }

      // begin() and end() for using range-for over positional args.
      typename std::vector<Tstring>::const_iterator begin()  const { return pos_args_.cbegin(); }
      typename std::vector<Tstring>::const_iterator end()    const { return pos_args_.cend();   }
      size_t size()                                 const { return pos_args_.size();   }

      //////////////////////////////////////////////////////////////////////////
      // Accessors

      // flag (boolean) accessors: return true if the flag appeared, otherwise false.
      bool operator[](Tstring const& name) const
	  {
		  return got_flag(name);
	  }

      // multiple flag (boolean) accessors: return true if at least one of the flag appeared, otherwise false.
      bool operator[](std::initializer_list<CharType const* const> init_list) const
	  {
		  return std::any_of(init_list.begin(), init_list.end(), [&](CharType const* const name) { return got_flag(name); });
	  }

	  // returns positional arg string by order. Like argv[] but without the options
	  Tstring const& operator[](size_t ind) const
	  {
		  if (ind < pos_args_.size())
			  return pos_args_[ind];
		  return empty_;
	  }

      // returns a std::istream that can be used to convert a positional arg to a typed value.
      Tistringstream operator()(size_t ind) const
	  {
		  if (pos_args_.size() <= ind)
			  return bad_stream();

		  return Tistringstream(pos_args_[ind]);
	  }

      // same as above, but with a default value in case the arg is missing (index out of range).
      template<typename T>
      Tistringstream operator()(size_t ind, T&& def_val) const
	  {
		  if (pos_args_.size() <= ind)
		  {
			  Tostringstream ostr;
			  ostr.precision(std::numeric_limits<long double>::max_digits10);
			  ostr << def_val;
			  return Tistringstream(ostr.str());
		  }

		  return Tistringstream(pos_args_[ind]);
	  }

      // parameter accessors, give a name get an std::istream that can be used to convert to a typed value.
      // call .str() on result to get as string
      Tistringstream operator()(Tstring const& name) const
	  {
		  auto optIt = params_.find(trim_leading_dashes(name));
		  if (params_.end() != optIt)
			  return Tistringstream(optIt->second);
		  return bad_stream();
	  }

      // accessor for a parameter with multiple names, give a list of names, get an std::istream that can be used to convert to a typed value.
      // call .str() on result to get as string
      // returns the first value in the list to be found.
      Tistringstream operator()(std::initializer_list<CharType const* const> init_list) const
	  {
		  for (auto& name : init_list)
		  {
			  auto optIt = params_.find(trim_leading_dashes(name));
			  if (params_.end() != optIt)
				  return Tistringstream(optIt->second);
		  }
		  return bad_stream();
	  }

      // same as above, but with a default value in case the param was missing.
      // Non-string def_val types must have an operator<<() (output stream operator)
      // If T only has an input stream operator, pass the string version of the type as in "3" instead of 3.
      template<typename T>
      Tistringstream operator()(Tstring const& name, T&& def_val) const
	  {
		  auto optIt = params_.find(trim_leading_dashes(name));
		  if (params_.end() != optIt)
			  return Tistringstream(optIt->second);

		  Tostringstream ostr;
		  ostr.precision(std::numeric_limits<long double>::max_digits10);
		  ostr << def_val;
		  return Tistringstream(ostr.str()); // use default
	  }

      // same as above but for a list of names. returns the first value to be found.
      template<typename T>
      Tistringstream operator()(std::initializer_list<CharType const* const> init_list, T&& def_val) const
	  {
		  for (auto& name : init_list)
		  {
			  auto optIt = params_.find(trim_leading_dashes(name));
			  if (params_.end() != optIt)
				  return Tistringstream(optIt->second);
		  }
		  Tostringstream ostr;
		  ostr.precision(std::numeric_limits<long double>::max_digits10);
		  ostr << def_val;
		  return Tistringstream(ostr.str()); // use default
	  }

   private:
      Tistringstream bad_stream() const
	  {
		  Tistringstream bad;
		  bad.setstate(std::ios_base::failbit);
		  return bad;
	  }

	  // JB: added the '/' special case
      Tstring trim_leading_dashes(Tstring const& name) const
	  {
		  auto pos = name.find_first_not_of('-');
		  if (!pos || (pos == Tstring::npos))
			  pos = name.find_first_not_of('/');
		  return Tstring::npos != pos ? name.substr(pos) : name;
	  }

      bool is_number(Tstring const& arg) const
	  {
		  // inefficient but simple way to determine if a string is a number (which can start with a '-')
		  Tistringstream istr(arg);
		  double number;
		  istr >> number;
		  return !(istr.fail() || istr.bad());
	  }
	  
	  // JB: added the '/' special case
	  bool is_option(Tstring const& arg) const
	  {
		  assert(0 != arg.size());
		  if (is_number(arg))
			  return false;
		  return (('-' == arg[0]) || ('/' == arg[0]));
	  }

      bool got_flag(Tstring const& name) const
	  {
		  return flags_.end() != flags_.find(trim_leading_dashes(name));
	  }

      bool is_param(Tstring const& name) const
	  {
		  return registeredParams_.count(name) ? true : false;
	  }

   private:
      std::vector<Tstring> args_;
      std::map<Tstring, Tstring> params_;
      std::vector<Tstring> pos_args_;
      std::multiset<Tstring> flags_;
      std::set<Tstring> registeredParams_;
      Tstring empty_;
   };


   //////////////////////////////////////////////////////////////////////////

   template<typename CharType>
   inline void parser<CharType>::parse(size_t argc, const CharType* const argv[], int mode /*= PREFER_FLAG_FOR_UNREG_OPTION*/)
   {
      // convert to strings
      args_.resize(argc);
      std::transform(argv, argv + argc, args_.begin(), [](const CharType* const arg) { return arg;  });

      // parse line
      for (auto i = 0u; i < args_.size(); ++i)
      {
         if (!is_option(args_[i]))
         {
            pos_args_.emplace_back(args_[i]);
            continue;
         }

         auto name = trim_leading_dashes(args_[i]);

         if (!(mode & NO_SPLIT_ON_EQUALSIGN))
         {
            auto equalPos = name.find('=');
            if (equalPos != Tstring::npos)
            {
               params_.insert({ name.substr(0, equalPos), name.substr(equalPos + 1) });
               continue;
            }
         }

         // if the option is unregistered and should be a multi-flag
         if (1 == (args_[i].size() - name.size()) &&         // single dash
            Mode::SINGLE_DASH_IS_MULTIFLAG & mode && // multi-flag mode
            !is_param(name))                                  // unregistered
         {
            Tstring keep_param;

            if (!name.empty() && is_param(Tstring(1ul, name.back()))) // last char is param
            {
               keep_param += name.back();
               name.resize(name.size() - 1);
            }

            for (auto const& c : name)
            {
               flags_.emplace(Tstring{ c });
            }

            if (!keep_param.empty())
            {
               name = keep_param;
            }
            else
            {
               continue; // do not consider other options for this arg
            }
         }

         // any potential option will get as its value the next arg, unless that arg is an option too
         // in that case it will be determined a flag.
         if (i == args_.size() - 1 || is_option(args_[i + 1]))
         {
            flags_.emplace(name);
            continue;
         }

         // if 'name' is a pre-registered option, then the next arg cannot be a free parameter to it is skipped
         // otherwise we have 2 modes:
         // PREFER_FLAG_FOR_UNREG_OPTION: a non-registered 'name' is determined a flag.
         //                               The following value (the next arg) will be a free parameter.
         //
         // PREFER_PARAM_FOR_UNREG_OPTION: a non-registered 'name' is determined a parameter, the next arg
         //                                will be the value of that option.

         assert(!(mode & Mode::PREFER_FLAG_FOR_UNREG_OPTION)
             || !(mode & Mode::PREFER_PARAM_FOR_UNREG_OPTION));

         bool preferParam = mode & Mode::PREFER_PARAM_FOR_UNREG_OPTION;

         if (is_param(name) || preferParam)
         {
            params_.insert({ name, args_[i + 1] });
            ++i; // skip next value, it is not a free parameter
            continue;
         }
         else
         {
            flags_.emplace(name);
         }
      };
   }
}
