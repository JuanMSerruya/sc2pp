#include <iostream>
#include <fstream>
#include <boost/program_options.hpp>
#include <libmpq/mpq.h>
#include <sc2pp/types.hpp>
#include <sc2pp/parsers.hpp>

namespace po = boost::program_options;
using namespace std;

template <typename Iterator>
void parse(Iterator begin, Iterator end)
{
    vector<sc2pp::game_event_ptr> messages = sc2pp::parse_events(begin, end);
    for (auto msg : messages)
    {
        cout << msg << "\n";
    }
}


int main(int argc, char** argv)
{
    po::options_description desc("Possible options");
    po::positional_options_description posdesc;
    po::variables_map vm;

    desc.add_options()
        ("help", "display this help message")
        ("input", po::value<vector<string>>(), "input file(s)")
        ("mpq", "treat input file as an MPQ archive (SC2Replay file), not as the replay.game.events file")
        ;
    posdesc.add("input", -1);
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .positional(posdesc)
              .run(),
              vm);
    po::notify(vm);

    const size_t BUFSIZE = 1 << 20; // 1MB

    if (vm.count("input") == 0)
    {
        if (vm.count("mpq"))
        {
            cerr << "Reading MPQ archives from stdin is not supported. Specify filename as an argument!" << endl;
            return 1;
        }
        typedef array<unsigned char, BUFSIZE> buffer_type;
        buffer_type buf;
        auto tobegin = begin(buf), toend = end(buf);
        cin >> noskipws; // don't swallow \n, \t, etc
        istream_iterator<char> frombegin(cin), fromend;
        size_t actual_size = 0;
        while (frombegin != fromend && tobegin != toend)
            *tobegin++ = *frombegin++, ++actual_size;
        
        parse(buf.cbegin(), buf.cbegin()+actual_size);
    }

    const auto& inputs = vm["input"].as<vector<string>>();
    for (const auto& input : inputs)
    {
        unique_ptr<unsigned char[]> buf;
        size_t actual_size = 0;
        if (vm.count("mpq"))
        {
            mpq_archive_s* archive;
            int ret = libmpq__archive_open(&archive, input.c_str(), -1);
            if (ret != 0)
            {
                cerr << "Unable to open MPQ archive: " << input << endl;
                return 2;
            }
            unsigned int fileno = 0;
            long size = 0, asize = 0;
            libmpq__file_number(archive, "replay.game.events", &fileno);
            libmpq__file_unpacked_size(archive, fileno, &size);
            buf.reset(new unsigned char[size]);
            libmpq__file_read(archive, fileno, buf.get(), size, &asize);
            actual_size = asize;
            libmpq__archive_close(archive);
        }
        else
        {
            ifstream file(input, ios_base::in | ios_base::binary);
            if (not file.good())
            {
                cerr << "Unable to open file: " << input << endl;
                return 2;
            }
            
            buf.reset(new unsigned char[BUFSIZE]);
            file >> noskipws;
            istream_iterator<char> frombegin(file), fromend;
            unsigned char* tobegin = buf.get(), *toend = buf.get()+BUFSIZE;
            while (frombegin != fromend && tobegin != toend)
                *tobegin++ = *frombegin++, ++actual_size;            
        }
        const unsigned char* begin = buf.get();
        parse(begin, begin+actual_size);
    }

    return 0;
}
