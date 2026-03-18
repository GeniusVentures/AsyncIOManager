
/// @brief this macro goes in your header file to create a Singleton class with a reference to the instance
/// @param the_class is the class that will be made into a singleton
#define SINGLETON_REF( the_class )                                                                                     \
private:                                                                                                               \
    /*! @brief Keep the class creation private, so it remains single (Singleton Pattern) */                            \
    the_class() {}                                                                                                     \
    /*! @brief delete the copy constructor so no duplicates can be made */                                             \
    the_class( const the_class & ) = delete;                                                                           \
    /*! @brief delete the assignment operator, so no additional copies are made available \
        @return actually null, since the operator is deleted and private */                            \
    the_class &operator=( const the_class & ) = delete;                                                                \
                                                                                                                       \
public:                                                                                                                \
    /*! @brief Keep the class creation private, so it remains single (Singleton Pattern) */                            \
    static the_class &GetInstance()                                                                                    \
    {                                                                                                                  \
        /*! @brief This static member variable holds the singleton instance. */                                        \
        static the_class _instance;                                                                                    \
        return _instance;                                                                                              \
    }

/// @brief this macro goes in your header file to create a Singleton class with a pointer to the instance
/// @param the_class is the class that will be made into a singleton
#define SINGLETON_PTR( the_class )                                                                                     \
private:                                                                                                               \
    /*! @brief Keep the class creation private, so it remains single (Singleton Pattern) */                            \
    the_class();                                                                                                       \
    /*! @brief delete the copy constructor so no duplicates can be made */                                             \
    the_class( const the_class & ) = delete;                                                                           \
    /*! @brief delete the assignment operator, so no additional copies are made available \
        @return actually null, since the operator is deleted and private */                            \
    the_class &operator=( const the_class & ) = delete;                                                                \
    /*! @brief This static member variable holds the singleton instance. */                                            \
    static the_class *_instance;                                                                                       \
                                                                                                                       \
public:                                                                                                                \
    /*! @brief Keep the class creation private, so it remains single (Singleton Pattern) */                            \
    static the_class *GetInstance()                                                                                    \
    {                                                                                                                  \
        if ( _instance == nullptr )                                                                                    \
        {                                                                                                              \
            _instance = new the_class();                                                                               \
        }                                                                                                              \
        return _instance;                                                                                              \
    }

/// @brief this macro goes in your cpp file to create a the instance of your Singleton class and set the static pointer
#define SINGLETON_PTR_INIT( the_class ) the_class *the_class::_instance = the_class::GetInstance();
