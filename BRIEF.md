Imagine if we wrote more catch2 tests. For example, this hypothetical function:


int do_stuff(int a) {
    if (a < 0) {
        return WT_RET_NOTFOUND;
    }

    int* foo = inner_func(a);

    if (!foo) {
        return WT_OMG;
    }

    return *foo;
}
We could write catch2 tests for this function like this:


// Given a negative input
// When do_stuff is called
// Then it should return WT_RET_NOTFOUND
int input = -1;
int result = do_stuff(input);
REQUIRE(result == WT_RET_NOTFOUND);

// Given a positive input
// When do_stuff is called
// And inner_func returns nullptr
// Then it should return WT_OMG
input = 5;
inner_func_fake.return_val = nullptr;
result = do_stuff(input);
REQUIRE(result == WT_OMG);

// Given a positive input
// When do_stuff is called
// And inner_func returns a valid pointer
// Then it should return the value pointed to by inner_func
input = 5;
int expected_value = 42;
inner_func_fake.return_val = &expected_value;
result = do_stuff(input);
REQUIRE(result == expected_value);
Basically what I mean is that we can write more comprehensive tests for our functions using catch2 and fff. We can check that inner functions are called, we can replace the inner function's return values and even write completely custom implementations.


REQUIRE(inner_func_fake.call_count == 3); // Check that inner_func was called three times
REQUIRE(inner_func_fake.last_arg == 5);   // Check that inner_func was called with the correct argument
inner_func_fake.return_val = 4;           // return 4

int* custom_inner_func(int) {
    return nullptr;
}

inner_func_fake.custom_impl = custom_inner_func; // Use the custom implementation for inner_func
The above is all possible with catch2 and fff.

Write tests for cur_layered.c. Use fff mocks to stub out complicated functions, to make testing the outer functions easier without needing real database objects. you can expose static functions as public if they deserve to be external. create fff fakes liberally - it's OK if it helps you challenge the caller. write bdd tests like @test/catch2/truncate/test_insert_truncate_entry.cpp with given when then, and short functions. 

The point of this is to demonstrate the value of catch2 and fff. catch2 on its own is not enough. use fff and show it off.
