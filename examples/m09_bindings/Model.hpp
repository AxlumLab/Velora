#pragma once

#include <string>
#include <vector>
#include <velora/data/Observable.hpp>
#include <velora/reflect/Markers.hpp>

namespace M09
{

rkclass Person : public velora::Observable
{
public:
    rkproperty(Name, std::string, get = name, set = setName) rkproperty(Age, int, get = age)

        Person() = default;
    Person(std::string name, int age) : m_name(std::move(name)), m_age(age) {}
    const std::string& name() const
    {
        return m_name;
    }
    void setName(std::string value)
    {
        set(m_name, std::move(value), "Name");
    }
    int age() const
    {
        return m_age;
    }
    void birthday()
    {
        ++m_age;
        raise("Age");
    }

private:
    std::string m_name;
    int m_age = 0;
};

// Shared by every window: each window's view binds to the same model.
rkclass Model : public velora::Observable
{
public:
    rkproperty(Query, std::string, get = query, set = setQuery)
        rkproperty(Summary, std::string, get = summary)
            rkproperty(People, velora::ObservableVector<Person>&, get = people)
                rkproperty(Selected, Person&, get = selected)

                    Model()
    {
        m_alice = Person("Alice", 30);
        m_bob = Person("Bob", 40);
    }
    const std::string& query() const
    {
        return m_query;
    }
    // Coerces to at most 12 bytes, so the TwoWay target shows what the source kept.
    void setQuery(std::string value)
    {
        ++setQueryCalls;
        if (value.size() > 12)
            value.resize(12);
        if (set(m_query, std::move(value), "Query"))
            raise("Summary");
    }
    std::string summary() const
    {
        return m_query.empty() ? "Type to search" : "Searching for " + m_query;
    }
    velora::ObservableVector<Person>& people()
    {
        return m_people;
    }
    const velora::ObservableVector<Person>& people() const
    {
        return m_people;
    }
    Person& selected()
    {
        return *m_selected;
    }
    const Person& selected() const
    {
        return *m_selected;
    }
    void selectBob(bool bob)
    {
        m_selected = bob ? &m_bob : &m_alice;
        raise("Selected");
    }
    Person& alice()
    {
        return m_alice;
    }
    Person& bob()
    {
        return m_bob;
    }

    int setQueryCalls = 0;

private:
    std::string m_query;
    velora::ObservableVector<Person> m_people;
    Person m_alice, m_bob;
    Person* m_selected = &m_alice;
};

// Controller of BindingPage.xaml (one per window).
rkclass Page
{
public:
    rkproperty(Model, Model&, get = model)

        explicit Page(M09::Model & model)
        : m_model(&model)
    {
    }
    M09::Model& model()
    {
        return *m_model;
    }
    const M09::Model& model() const
    {
        return *m_model;
    }

    // Click handler of the item template's button: receives the container's current item.
    cfunc void Remove(Person & person)
    {
        removed.push_back(person.name());
        auto& people = m_model->people();
        for (std::size_t i = 0; i < people.size(); ++i)
            if (&people[i] == &person)
                return people.erase(i);
    }

    std::vector<std::string> removed;

private:
    M09::Model* m_model;
};

} // namespace M09
