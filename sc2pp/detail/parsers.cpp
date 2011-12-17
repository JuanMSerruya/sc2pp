#include <boost/spirit/include/phoenix.hpp>
#include <sc2pp/detail/parsers.hpp>

#include <array>


using namespace boost::spirit::qi;
using namespace boost::phoenix;
using std::array;
using sc2pp::detail::hugenum_t;

namespace
{
    template<class OutIter>
    OutIter write_escaped(const unsigned char* begin, const unsigned char* end, OutIter out) {
        *out++ = '"';
        for (const unsigned char* i = begin; i != end; ++i) {
            unsigned char c = *i;
            if (' ' <= c and c <= '~' and c != '\\' and c != '"') {
                *out++ = (char)c;
            }
            else {
                *out++ = '\\';
                switch(c) {
                case '"':  *out++ = '"';  break;
                case '\\': *out++ = '\\'; break;
                case '\t': *out++ = 't';  break;
                case '\r': *out++ = 'r';  break;
                case '\n': *out++ = 'n';  break;
                default:
                    char const* const hexdig = "0123456789ABCDEF";
                    *out++ = 'x';
                    *out++ = hexdig[c >> 4];
                    *out++ = hexdig[c & 0xF];
                }
            }
        }
        *out++ = '"';
        return out;
    }
    
    template <typename Context>
    void errorhandler(boost::fusion::vector<const unsigned char*, const unsigned char*, 
                                            const unsigned char*, const info&> params, 
                      Context, error_handler_result)
    {
        const int MAX_CONTEXT = 20;
        std::cerr << "Error! Expecting " << at_c<3>(params) << " here: ";
        auto begin = at_c<0>(params), end = at_c<2>(params);
        if (end - begin > MAX_CONTEXT) begin = end - MAX_CONTEXT;
        write_escaped(begin, end, std::ostream_iterator<char>(std::cerr));
        std::cerr << " >>><<< ";
        begin = at_c<2>(params); end = at_c<1>(params);
        if (end - begin > MAX_CONTEXT) end = begin + MAX_CONTEXT;
        write_escaped(begin, end, std::ostream_iterator<char>(std::cerr));
        std::cerr << std::endl;
    }

    struct apply_sign_impl
    {
        template <typename Arg>
        struct result
        {
            typedef long type;
        };
        template <typename Arg>
        long operator()(Arg num) const
        {
            return (num / 2) * (1 - 2 * (num & 1));
        }
        
    };

    struct apply_huge_sign_impl
    {
        template <typename Arg>
        struct result
        {
            typedef hugenum_t type;
        };
        template <typename Arg>
        hugenum_t operator()(Arg num) const
        {
            return (num / 2) * (1 - 2 * (num % 2));
        }
        
    };
    
    function<apply_sign_impl> apply_sign;
    function<apply_huge_sign_impl> apply_huge_sign;

    struct vector_to_array_impl
    {
        template <typename Arg>
        struct result
        {
            typedef std::array<unsigned char, 4> type;
        };
        template <typename Arg>
        typename result<Arg>::type operator()(Arg a) const
        {
            typename result<Arg>::type ret;
            for (int i = 0; i < ret.size() && i < a.size(); ++i)
            {
                ret[i] = a[i];
            }
            return ret;
        }
    };

    function<vector_to_array_impl> vector_to_array;

    template <typename Base>
    struct base_from_derived_visitor : public boost::static_visitor<std::shared_ptr<Base> >
    {
        template <typename DerivedPtr>
        std::shared_ptr<Base> operator()(DerivedPtr const & arg) const
        {
            return std::dynamic_pointer_cast<Base>(arg);
        }
    };

    // only for the weak:
    struct dynamic_pointer_cast_impl
    {
        template <typename Arg>
        struct result
        {
            typedef sc2pp::message_event_t rawtype;
            typedef std::shared_ptr<rawtype> type;
        };
        template <typename Arg>
        typename result<Arg>::type operator()(Arg a) const
        {
            base_from_derived_visitor<typename result<Arg>::rawtype> v;
            return boost::apply_visitor(v, a);
        }
    };
    function<dynamic_pointer_cast_impl> dynamic_pointer_cast_; // make sure we notice if they actually implement this in phoenix
}

namespace sc2pp { 
    namespace parsers {
        
        byte_string_rule_type byte_string;
        single_byte_integer_rule_type single_byte_integer;
        single_byte_integer_rule_type single_byte_integer_; // just an integer without 'type' byte
        four_byte_integer_rule_type four_byte_integer;
        variable_length_integer_rule_type variable_length_integer;
        array_rule_type array;
        map_rule_type map;
        object_rule_type object;

        timestamp_rule_type timestamp;
        ping_event_rule_type ping_event;
        message_rule_type message;
        unknown_message_rule_type unknown_message;
        message_event_rule_type message_event;

        game_events_rule_type game_events;
        game_event_rule_type game_event;
        unknown_event_rule_type unknown_event;
        player_joined_event_rule_type player_joined_event;
        game_started_event_rule_type game_started_event;
        initial_event_rule_type initial_event;
        action_event_rule_type action_event;
        player_left_event_rule_type player_left_event;
        resource_transfer_event_rule_type resource_transfer_event;
        resource_rule_type resource;

        Initializer::Initializer()
        {
            byte_string %=
                omit[byte_(0x2)] > omit[byte_[_a = _1/2]] > repeat(_a)[byte_];
	    
            single_byte_integer_ =
                byte_[_val = apply_sign(_1)];

            single_byte_integer %=
                omit[byte_(0x6)] > single_byte_integer_;

            four_byte_integer =
                omit[byte_(0x7)] > big_dword[_val = apply_sign(_1)];

            variable_length_integer =
                omit[byte_(0x9)] > repeat(0, inf)[&byte_[if_((_1 & 0x80) == 0)[_pass = false]]
                                                  >> byte_[_a += static_cast_<hugenum_t>(_1 & 0x7f) << (_b * 7)] 
                                                  >> eps[_b += 1]]
                > byte_[_a += static_cast_<hugenum_t>(_1 & 0xff) << (_b * 7)] 
                > eps[_val = apply_huge_sign(_a)];

            array %=
                omit[byte_(0x4) > byte_(0x1) > byte_(0x0) > single_byte_integer_[_a = _1]]
                >> repeat(_a)[object];

            map =
                omit[byte_(0x5) > single_byte_integer_[_a = _1]]
                >> repeat(_a)[single_byte_integer_ > object];

            object %=  
                byte_string | single_byte_integer | four_byte_integer | variable_length_integer
                | array | map;
          
            timestamp =
                &byte_[_b = _1 & 0x3] >> byte_[_a = static_cast_<int>(_1) >> 2]
                                      >> repeat(_b)[eps[_a <<= 8] >> byte_[_a += _1]]
                                      >> eps[_val = _a];
            
            ping_event =
                byte_(0x83) > little_dword[_a = _1] > little_dword[_b = _1] 
                >> eps[_val = boost::phoenix::bind(ping_event_t::make, _r1, _r2, _a, _b)];
            
            message = 
                &byte_[if_((_1 & 0x80) != 0)[_pass = false]]
                > byte_[_b = static_cast_<message_t::target_t>(_1 & 0x3), _a = (_1 & 0x18) << 3] >> byte_[_a += _1]
                >> as_string[repeat(_a)[byte_]][_val = boost::phoenix::bind(message_t::make, _r1, _r2, _b, _1)];

            unknown_message =
                byte_(0x80) > repeat(4)[byte_][_val = boost::phoenix::bind(unknown_message_t::make, _r1, _r2, vector_to_array(_1))];

            message_event %= omit[timestamp[_a = _1] >> byte_[_b = _1 & 0xF]]
                >> (ping_event(_a, _b) | message(_a, _b) | unknown_message(_a, _b));

            game_events %= *game_event;
            
            game_event %=
                omit[timestamp[_a = _1] >> &byte_[_b = (static_cast_<int>(_1) >> 3) & 0x1f]] >>
                initial_event(_a, _b) | action_event(_a, _b) |
                unknown_event(_a, _b);

            unknown_event = 
                byte_[_a = _1 & 0x7] 
                >> byte_[_val = boost::phoenix::bind(unknown_event_t::make, _r1, _r2, _a, _1)];

            initial_event %=
                omit[byte_[if_((_1 & 0x7) != 0)[_pass = false]]]
                > player_joined_event(_r1, _r2) | game_started_event(_r1, _r2);

            player_joined_event = 
                (byte_(0xB) | byte_(0xC) | byte_(0x2C))
                >> eps[_val = boost::phoenix::bind(player_joined_event_t::make, _r1, _r2)];

            game_started_event =
                byte_(0x5)
                >> eps[_val = boost::phoenix::bind(game_started_event_t::make, _r1, _r2)];

            action_event %=
                omit[byte_[if_((_1 & 0x7) != 1)[_pass = false]]]
                > player_left_event(_r1, _r2) | resource_transfer_event(_r1, _r2);

            player_left_event =
                byte_(0x9)
                >> eps[_val = boost::phoenix::bind(player_left_event_t::make, _r1, _r2)];
                
            resource_transfer_event =
                &byte_[if_((_1 & 0xf) != 0xf)[_pass = false]] >> byte_[_a = static_cast_<int>(_1) >> 4] >> 
                byte_(0x84) >>
                repeat(4)[resource][_val = boost::phoenix::bind(resource_transfer_event_t::make, _r1, _r2, _a, _1)];
                                                             
            resource = 
                big_dword[_val = (static_cast_<num_t>(_1) >> 8) * (static_cast_<num_t>(_1) & 0xf0) + (static_cast_<num_t>(_1) & 0x0f)];


            // camera_event %=
            //     omit[byte_[if_((_1 & 0x7) != 3)[_pass = false]]];


#define HANDLE_ERROR(X)                                                 \
            X.name(#X);                                                 \
            on_error<fail>(X, ::errorhandler<X##_rule_type::context_type>)

            HANDLE_ERROR(byte_string);
            HANDLE_ERROR(single_byte_integer);
            HANDLE_ERROR(four_byte_integer);
            HANDLE_ERROR(variable_length_integer);
            HANDLE_ERROR(array);
            HANDLE_ERROR(map);
            HANDLE_ERROR(object);
            HANDLE_ERROR(timestamp);
            HANDLE_ERROR(message_event);
            HANDLE_ERROR(ping_event);
            HANDLE_ERROR(message);
            HANDLE_ERROR(unknown_message);
            HANDLE_ERROR(game_events);
            HANDLE_ERROR(game_event);
            HANDLE_ERROR(unknown_event);
            HANDLE_ERROR(player_joined_event);
            HANDLE_ERROR(game_started_event);
            HANDLE_ERROR(initial_event);
            HANDLE_ERROR(action_event);
            HANDLE_ERROR(player_left_event);
            HANDLE_ERROR(resource_transfer_event);
        }

        static Initializer __initializer;
    }
}

// Local Variables:
// mode:c++
// c-file-style: "stroustrup"
// end:
