#include "broker/broker.hh"
#include "broker/endpoint.hh"
#include "broker/store/master.hh"
#include "broker/store/clone.hh"
#include "broker/store/frontend.hh"
#include "broker/store/response_queue.hh"
#include "testsuite.hh"
#include <iostream>
#include <map>
#include <vector>
#include <unistd.h>
#include <poll.h>

using namespace std;
using dataset = map<broker::data, broker::data>;

dataset get_contents(const broker::store::frontend& store)
	{
	dataset rval;

	for ( const auto& key : broker::store::keys(store) )
		{
		auto val = broker::store::lookup(store, key);
		if ( val ) rval.insert(make_pair(key, *val.get()));
		}

	return rval;
	}

bool compare_contents(const broker::store::frontend& store, const dataset& ds)
	{
	return get_contents(store) == ds;
	}

bool compare_contents(const broker::store::frontend& a,
                      const broker::store::frontend& b)
	{
	return get_contents(a) == get_contents(b);
	}

void wait_for(const broker::store::frontend& f, broker::data k,
              bool exists = true)
	{
	while ( broker::store::exists(f, k) != exists ) usleep(1000);
	}

int main(int argc, char** argv)
	{
	broker::init();
	broker::endpoint server("server");
	broker::store::master master(server, "mystore");

	dataset ds0 = { make_pair("1", "one"),
	                make_pair("2", "two"),
	                make_pair("3", "three") };
	for ( const auto& p : ds0 ) master.insert(p.first, p.second);

	BROKER_TEST(compare_contents(master, ds0));

	// TODO: better way to distribute ports among tests so they can go parallel
	if ( ! server.listen(9999, "127.0.0.1") )
		{
		cerr << server.last_error() << endl;
		return 1;
		}

	broker::endpoint client("client");
	broker::store::frontend frontend(client, "mystore");
	broker::store::clone clone(client, "mystore",
	                          std::chrono::duration<double>(0.25));

	client.peer("127.0.0.1", 9999).handshake();

	BROKER_TEST(compare_contents(frontend, ds0));
	BROKER_TEST(compare_contents(clone, ds0));

	master.insert("5", "five");
	BROKER_TEST(*broker::store::lookup(master, "5") == "five");
	BROKER_TEST(compare_contents(frontend, master));
	BROKER_TEST(compare_contents(clone, master));

	master.erase("5");
	BROKER_TEST(!broker::store::exists(master, "5"));
	BROKER_TEST(compare_contents(frontend, master));
	BROKER_TEST(compare_contents(clone, master));

	frontend.insert("5", "five");
	wait_for(master, "5");
	BROKER_TEST(compare_contents(frontend, master));
	BROKER_TEST(compare_contents(clone, master));

	frontend.erase("5");
	wait_for(master, "5", false);
	BROKER_TEST(compare_contents(frontend, master));
	BROKER_TEST(compare_contents(clone, master));

	clone.insert("5", "five");
	wait_for(master, "5");
	BROKER_TEST(compare_contents(frontend, master));
	BROKER_TEST(compare_contents(clone, master));

	clone.erase("5");
	wait_for(master, "5", false);
	BROKER_TEST(compare_contents(frontend, master));
	BROKER_TEST(compare_contents(clone, master));

	master.clear();
	BROKER_TEST(broker::store::size(master) == 0);
	BROKER_TEST(compare_contents(frontend, master));
	BROKER_TEST(compare_contents(clone, master));

	broker::done();
	return BROKER_TEST_RESULT();
	}