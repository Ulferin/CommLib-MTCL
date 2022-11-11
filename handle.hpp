#ifndef HANDLE_HPP
#define HANDLE_HPP

#include <iostream>

class ConnType;

class Handle {
    // Potrebbe avere un ID univoco incrementale che definiamo noi in modo da
    // accedere velocemente a quale connessione fa riferimento nel ConnType
    // Questo potrebbe sostituire l'oggetto "this" a tutti gli effetti quando
    // vogliamo fare send/receive/yield --> il ConnType di appartenenza ha info
    // interne per capire di chi si tratta
    friend class HandleUser;
    ConnType* parent;

public:
    Handle(ConnType* parent) : parent(parent) {}
    virtual void send(char* buff, size_t size);
    virtual void receive(char* buff, size_t size);
};

#endif
