#include <iostream>

#include <vector>
#include <string>
#include <cstring>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

enum class t_Mode {
	que = 0,
	add = 1,
	rem = 2,
	del = 3,
	era = 4,
	inf = 5

};

namespace Config{
	t_Mode mode;
	std::string file;
	bool display_info;
};


const char* const help_string = ""
	"library mode parameters ...\n"
	"-h is the only parameter that can be used where mode should go\n"
	"\n"
	"modes:\n"
	"   info name\n"
	"      displays info about particular entry\n"
	"   add name [tags]\n"
	"      adds tags to the name\n"
	"      can be later searched with query\n"
	"      (see query mode to see what is [tags])\n"
	"   remove name [tags]\n"
	"      removes tag from the entry\n"
	"      (see query mode to see what is [tags])\n"
	"   delete name\n"
	"      removes entry completely\n"
	"   erase [tag]\n"
	"      removes [tag] completely\n"
	"      (see query mode to see what is [tag])\n"
	"   query [query]\n"
	"       the syntax below uses BNF\n"
	"\n"
	"       query    ::= operator query\n"
	"                  | tag query\n"
	"                  | tag\n"
	"                  | \"(\" query \")\"\n"
	"\n"
	"       operator ::= \"AND\"\n"
	"                  | \"OR\"\n"
	"\n"
	"       tags     ::= tag tags\n"
	"                  | tag\n"
	"\n"
	"       tag      ::= <desired string>\n"
	"\n"
	"    if the operator is AND then query returns those tags that matched ALL subqueries\n"
	"    if the operator is OR  then query returns those tags taht matched ANY subqueries\n"
	"    results of top level query are printed to stdout\n"
	"\n"
	"    if nested query is not enclosed in parentheses then it is assumed to extend to the end of the input\n"
	"    top level query is assumed to start with AND so it is not necessary to specify it\n"
	"\n"
	"parameters:\n"
	"    -h   display this Help, ignores any parameters listed below and query\n"
	"    -f   use following File for whatever mode requires\n"
	"";



struct t_Query{
	bool is_tag;
	
	union{
		int list_index;
		int tag_index;
	};

	t_Query(){}
	t_Query(bool const n_is_tag, int const n_index)
		: is_tag(n_is_tag), list_index(n_index) {}
};

struct t_Query_List{
	bool is_and;
	std::vector<t_Query> subqueries;
};

std::vector<t_Query_List> query_list_list;
std::vector<std::string>  tags_list;


int depth = 0;
void print_query_list(t_Query_List const& ql)
{
	for(int i = 0; i < depth; i++)
		std::cout << "  ";
	
	if(ql.is_and)
		std::cout << "AND\n";
	else
		std::cout << "OR\n";
	
	depth++;
	for(auto const& query : ql.subqueries)
	{
		if(query.is_tag)
		{
			for(int i = 0; i < depth; i++)
				std::cout << "  ";

			std::cout << tags_list[query.tag_index] << '\n';
		}
		else
			print_query_list(query_list_list[query.list_index]);

	}
	depth--;

	return;
}

bool create_query_helper(std::string const& request, int beg, int end)
{
	//implies that request also ends with ')'
	//otherwise there would have been error when parsing
	//takes into account additional ' ' 
	if(request[beg] == '(')
	{
		beg += 1;
		end -= 1;
	}
	while(beg <= end && request[beg] == ' ')
		beg++;
	while(end >= beg && request[end] == ' ')
		end--;

	bool is_and;
	     if(0 == strncmp(request.c_str() + beg, "AND", 3))
	{
		beg += 4;
		is_and = true;
	}
	else if(0 == strncmp(request.c_str() + beg, "OR",  2))
	{
		beg += 3;
		is_and = false;
	}
	else
	{
		std::cout << "ERROR: query does not start with AND or OR\n"
		             "       on character " << beg  << "\n";
		
		return false;
	}

	int const cur_query_list_id = query_list_list.size();
	query_list_list.emplace_back();
	query_list_list[cur_query_list_id].is_and = is_and;

	while(beg <= end && request[beg] == ' ')
		beg++;

	//beg points to second word, if it exists
	if(beg > end)
	{
		std::cout << "ERROR: query does not contain any tags or subqueries\n"
		             "       on character " << beg << "\n";
		
		return false;
	}

	while(beg <= end)
	{
		//subquery
		if(request[beg] == '(')
		{
			//move to the right, find matching parentheses
			int depth = 0;
			int cur = beg;
			while(beg <= end)
			{
				if('(' == request[beg])
					depth++;
				else if(')' == request[beg])
				{
					depth--;

					if(0 == depth)
						break;
				}

				beg++;
			}

			if(0 != depth)
			{
				std::cout << "ERROR: unmatched parentheses\n"
							 "       starting from character " << cur << "\n";
				
				return false;
			}

			//points to next query created, which is result of the call below
			query_list_list[cur_query_list_id].subqueries.emplace_back(false, query_list_list.size());
			create_query_helper(request, cur, beg);

			//next word
			while(beg <= end && request[beg] == ' ')
				beg++;
			continue;
		}

		//find where the word ends
		int cur = beg;
		while(beg <= end
		   && request[beg] != ' ')
			beg++;
		//cur beg inclusive
		//check for possibility of AND or OR
		if(0 == strncmp(request.c_str() + cur, "AND", 3)
		|| 0 == strncmp(request.c_str() + cur, "OR",  2))
		{
			//points to next query created, which is result of the call below
			query_list_list[cur_query_list_id].subqueries.emplace_back(false, query_list_list.size());
			//nothing more to do here
			return create_query_helper(request, cur, end);
		}

		int cur_end = beg;
		//adjust cur until starts with [a-zA-Z0-9-_]
		while(true)
		{
			if(request[cur] >= 'a' && request[cur] <= 'z')
				break;
			if(request[cur] >= 'A' && request[cur] <= 'Z')
				break;
			if(request[cur] >= '0' && request[cur] <= '9')
				break;
			if(request[cur] == '_' && request[cur] == '-')
				break;
			cur++;
		}
		while(cur_end > 0)
		{
			if(request[cur_end] >= 'a' && request[cur_end] <= 'z')
				break;
			if(request[cur_end] >= 'A' && request[cur_end] <= 'Z')
				break;
			if(request[cur_end] >= '0' && request[cur_end] <= '9')
				break;
			if(request[cur_end] == '_' && request[cur_end] == '-')
				break;
			cur_end--;
		}

		if(cur_end - cur >= 0)
		{
			tags_list.emplace_back(request.c_str() + cur, cur_end - cur + 1);
			query_list_list[cur_query_list_id].subqueries.emplace_back(true, tags_list.size() - 1);
		}
		//next word
		while(beg <= end && request[beg] == ' ')
			beg++;
		continue;

	}

	return true;
}

//first one does not need to have initial AND
//and also sets up query_list_list
bool create_query(std::string const& request)
{
	return create_query_helper(request, 0, request.size() - 1);
}

std::vector<std::string> evaluate_query_list(t_Query_List const& ql, char const* const data, int const size);

std::vector<std::string> evaluate_query(t_Query const& q, char const* const data, int const size)
{
	if(q.is_tag == false)
		return evaluate_query_list(query_list_list[q.list_index], data, size);
	

	std::vector<std::string> entries;
	std::string const& str = tags_list[q.tag_index];
	//search data for a given string
	int off = 0;
	while(off < size)
	{
		if(0 == strncmp(str.c_str(), data + off, str.size())
		&&   ( ' '  == data[off + str.size()]
		    || '\n' == data[off + str.size()]))
		{
			//skip to first word
			off += str.size() + 1;

			while(off < size)
			{
				//parse and insert words
				int cur = off;

				while(off < size && data[off] != ' ' && data[off] != '\n')
					off++;


				entries.emplace_back(data + cur, off - cur);

				if(data[off] == '\n')
					break;

				off++;

			}

			return entries;
		}

		//move to next line
		while(off < size && data[off] != '\n')
			off++;
		off++;

	}


	return entries;
}

std::vector<std::string> evaluate_query_list(t_Query_List const& ql, char const* const data, int const size)
{
	std::vector<std::string> entries;

	std::vector<std::vector<std::string>> subresults;	

	for(auto const& query : ql.subqueries)
		subresults.emplace_back(evaluate_query(query, data, size));

	entries = subresults[0];
	if(true == ql.is_and)
	{
		std::vector<std::string> entries_new;
		for(int i = 1; i < static_cast<int>(subresults.size()); i++)
		{
			for(auto const& str : subresults[i])
			{
				//if present in result, add
				for(auto const& str_present : entries)
				{
					if(str == str_present)
					{
						entries_new.emplace_back(str);
						goto and_next_str;
					}
				}

			and_next_str:
				continue;
			}

			entries = entries_new;
			entries_new.clear();
		}
	}
	else
	{
		for(int i = 1; i < static_cast<int>(subresults.size()); i++)
		{
			for(auto const& str : subresults[i])
			{
				//if not present in result, add
				for(auto const& str_present : entries)
				{
					if(str == str_present)
						goto or_next_str;
				}

				entries.emplace_back(str);
			or_next_str:
				continue;
			}
		}
	}

	return entries;
}

int main(int const argc, char const* const argv[])
{
	if(1 == argc)
	{
		std::cout << "Not enough arguments, see -h for more info\n";
		return 1;
	}

	//check mode
	//names are fully distinguishable by first letter 
	switch(argv[1][0])
	{
		case 'q':
			Config::mode = t_Mode::que;
			Config::file = "./tags.txt";
			break;
		case 'a':
			Config::mode = t_Mode::add;
			Config::file = "./tags.txt";
			break;
		case 'r':
			Config::mode = t_Mode::rem;
			Config::file = "./tags.txt";
			break;
		case 'd':
			Config::mode = t_Mode::del;
			Config::file = "./tags.txt";
			break;
		case 'e':
			Config::mode = t_Mode::era;
			Config::file = "./tags.txt";
			break;
		case 'i':
			Config::mode = t_Mode::inf;
			Config::file = "./desc.txt";
			break;
		case '-':
			if(strlen(argv[1]) != 2) goto invalid_mode;
			if ('h' != argv[1][1])   goto invalid_mode;
		[[fallthrough]];
		case 'h':
			std::cout << help_string;

			return 0; //yes, return
		invalid_mode:
		default:
			std::cout << "Not a valid mode, see -h for more info\n";
			return 2; //yes, return
	}

	std::string request;
	std::vector<char const*> args;

	//parse arguments
	for(int i = 2; i < argc; i++)
	{
		//read request
		char const* const cur_arg = argv[i];
		if(cur_arg[0] != '-')
		{
			if(Config::mode == t_Mode::que
			|| Config::mode == t_Mode::inf)
			{
				request += cur_arg;
				request += " ";
			}
			else
			{
				args.emplace_back(argv[i]);
			}
			continue;
		}

		//parse options
		int const len = strlen(cur_arg);

		for(int j = 1; j < len; i++)
		{
			switch(cur_arg[j])
			{
			case 'h':
				std::cout << help_string;
				//YES, RETURN
				return 0; 
			case 'f':
				if(argc == i + 1)
				{
					std::cout << "No filename following -f\n";
					return 3;
				}
				Config::file = argv[i + 1];
				i++;
				goto break_parsing;
			}
		}
	break_parsing:
		continue;
	}
	if(0 == request.size() && 0 == args.size())
	{
		std::cout << "request cannot be empty\n";
		return 4;
	}
	request.pop_back(); //remove trailing space
	//load the file into memory
	//it is much faster than c++ file io
	int const fd = open(Config::file.c_str(), O_RDWR);
	if(-1 == fd)
	{
		perror("could not open file");
		return 5; 
	}
	int const size = lseek(fd, 0, SEEK_END);
	char* data = static_cast<char*>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
	if(MAP_FAILED == data)
	{
		perror("failed to allocate memory");
		return 6;
	}


	switch(Config::mode)
	{
	case t_Mode::inf:
	{

		int offset = 0;
		bool found = false;
		while(offset < size)
		{
			//beggining of a line
			//tab => info for previous name
			if(data[offset] == '\t')
			{
				while(offset < size
				   && data[offset] != '\n')
					offset++;

				//offset is either >= size or data[offset] == '\n'
				//if latter, skip current, if former does not matter
				offset++;
				continue;
			}

			//check whether name matches
			if(0 != strncmp(request.c_str(), data + offset, request.size()))
			{
				//does not match, skip to next line
				while(offset < size
				   && data[offset] != '\n')
					offset++;
				offset++;
				continue;
			}
			//matches, advance ptr until next entry found
			int end = offset + 1; //adjust so offset = 0 does not hurt
			while(end < size)
			{
				if(data[end - 1] == '\n'
				&& data[end]     != '\t')
					break;

				end++;
			}
			//end is EXCLUSIVE
			printf("%.*s", end - offset, data + offset);	
			found = true;
			break;
				
		}
		if(!found)
			std::cout << "Did not find entry\n";

		//breaks from switch and clears up data
		break;
	}
	//tag management
	case t_Mode::que:
	{
		//possible error message printed by validate_query
		if(!create_query(request))
		{
			munmap(data, size);		
			close(fd);
			return 7;
		}

//		print_query_list(query_list_list[0]);

		//now, evaluate the tags
		auto const& result = evaluate_query_list(query_list_list[0], data, size);

		for(auto const& str : result)
			std::cout << str << '\n';

		//breaks from switch and clears up data
		break;
	}
	case t_Mode::era:
	{
		//find a tag
		//search data for a given string
		int beg = -1;
		int end = -1;
		int off = 0;

		int const arg_len = strlen(args[0]);

		while(off < size)
		{
			if(0 == strncmp(args[0], data + off, arg_len)
			&&   ( ' '  == data[off + arg_len]
			    || '\n' == data[off + arg_len]))
			{
				beg = off;

				//move to next line
				while(off < size && data[off] != '\n')
					off++;

				end = off + 1;

				memmove(data + beg, data + end, size - end);
				ftruncate(fd, size - (end - beg));
				goto free_resources;
			}
			//move to next line
			while(off < size && data[off] != '\n')
				off++;
			off++;
		}

		if(-1 == beg)
			std::cout << "Did not find tag\n";
		else
			std::cout << "Tag successfully removed\n";

		break;
	}
	case t_Mode::del:
	{
		//first mark names
		int off =  0;
		bool found = false;
		char constexpr removed_mark = 2;

		char const* const name = args[0];
		int const name_size = strlen(name);

		//on each line skip first word as it is tag
		while(off < size)
		{
			while(off < size && data[off] != '\n' && data[off] != ' ')
				off++;

			if(off >= size)
				break;

			if(data[off] == '\n')
			{
				off++;
				continue;
			}

			while(data[off] == ' ')
				off++;

			if(0 != strncmp(data + off, name, name_size)
			||   ( ' '  != data[off + name_size]
			    && '\n' != data[off + name_size]))
				continue;

			//so the space is properly removed
			off--;
			for(int i = 0; i < name_size + 1; i++)
				data[off + i] = removed_mark;

			found = true;
		}

		if(!found)
		{	
			std::cout << "Did not find any matching entry\n";
			break;
		}
		

		//now adjust
		int new_size = 0;
		for(int i = 0; i < size; i++)
		{
			if(data[i] == removed_mark)
				continue;

			data[new_size] = data[i];
			new_size++;
		}

		ftruncate(fd, new_size);
		break;

	}
	case t_Mode::rem:
	{
		//separate name and the tag 
		if(1 == args.size())
		{
			std::cout << "ERROR: request lacks tags\n";
			break;
		}

		char const * const name = args[0];

		int const name_size = strlen(name);

		//first mark names
		int off =  0;
		bool found = false;
		char constexpr removed_mark = 2;


		bool nline = true;
		//compare first tag
		while(off < size)
		{
			//if tag differs, skip to next line
			if(nline)
			{
				bool match = false;
				for(int i = 1; i < static_cast<int>(args.size()); i++)
				{
					int const len = strlen(args[i]);
					if(0 != strncmp(args[i], data + off, len)
					||   ( ' '  != data[off + len]
						&& '\n' != data[off + len]))
						continue;

					match = true;
					break;
				}

				if(!match)
				{
					while(off < size && data[off] != '\n')
						off++;

					off++;
					continue;
				}

				nline = false;
			}

			while(off < size && data[off] != '\n' && data[off] != ' ')
				off++;

			if(off >= size)
				break;

			if(data[off] == '\n')
			{
				nline = true;
				off++;
				continue;
			}

			while(data[off] == ' ')
				off++;

			if(0 != strncmp(data + off, name, name_size)
			||   ( ' '  != data[off + name_size]
				&& '\n' != data[off + name_size]))
				continue;

			//so the space is properly removed
			off--;
			for(int i = 0; i < name_size + 1; i++)
				data[off + i] = removed_mark;

			found = true;
			break;
		}

		if(!found)
		{	
			std::cout << "Did not find any matching entry\n";
			break;
		}
		

		//now adjust
		int new_size = 0;
		for(int i = 0; i < size; i++)
		{
			if(data[i] == removed_mark)
				continue;

			data[new_size] = data[i];
			new_size++;
		}

		ftruncate(fd, new_size);
		break;

	}
	case t_Mode::add:
	{
		if(1 == args.size())
		{
			std::cout << "ERROR: lack of tags to add\n";
			break;
		}

		char const* const name = args[0];

		std::string new_file;
		//some assumption, probably too high
		new_file.reserve(size + size/2);

		std::vector<bool> added(args.size(), false);

		int off = 0;
		while(off < size)
		{
			//compare tag to see if it maches with anything
			bool match = false;
			for(int i = 1; i < static_cast<int>(args.size()); i++)
			{
				if(added[i])
					continue;

				int const len = strlen(args[i]);
				if(0 != strncmp(args[i], data + off, len)
				||   ( ' '  != data[off + len]
					&& '\n' != data[off + len]))
					continue;

				match = true;
				added[i] = true;
				break;
			}

			//skip to next line
			if(!match)
			{
				int const cur = off;
				while(off < size && data[off] != '\n')
					off++;
				off++;

				new_file.append(data + cur, off - cur);
				continue;
			}

			//add tag
			//move until newline
			int cur = off;
			while(off < size && data[off] != '\n')
				off++;
			new_file.append(data + cur, off - cur);
			new_file.append(" ");
			new_file.append(name);
			new_file.append("\n");
			off++;

		}
		//add the not added
		for(int i = 1; i < static_cast<int>(args.size()); i++)
		{
			if(added[i])
				continue;

			new_file.append(args[i]);
			new_file.append(" ");
			new_file.append(name);
			new_file.append("\n");
		}

		//due to updating file, mmap is cleaned earlier
		munmap(data, size);
		close(fd);

		//because fd in outer scope is const
		//this is ok
		int const fd = open(Config::file.c_str(), O_WRONLY);
		write(fd, new_file.c_str(), new_file.size());
		close(fd);
	
		return 0;
	}
	} //switch(T_config

free_resources:
	munmap(data, size);		
	close(fd);
	return 0;
}
