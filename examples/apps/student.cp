/* Student Records — a single-file C+ CRUD application.
 * Modern C+: class, constructor, this-> methods, auto, array .len, print.
 * Data is stored in students.db (portable line-based text format).
 * Build: ../build/c+ build students.cp -o students && ./students
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "cplus/cplus.h"

#define DATA_FILE "students.db"
#define MAX_STUDENTS 100

typedef struct {
    int id;
    char name[64];
    int age;
    double grade;
} Student;

/* Line input: never allow a long line to spill into the next prompt. */
int read_line(const char *prompt, char *dst, size_t size) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(dst, (int)size, stdin)) return 0;
    size_t n = strlen(dst);
    if (n && dst[n - 1] == '\n') {
        dst[n - 1] = '\0';
    } else {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF) { }
    }
    return 1;
}

int read_int(const char *prompt, int min, int max, int *out) {
    char line[80];
    while (read_line(prompt, line, sizeof(line))) {
        char *end;
        errno = 0;
        long value = strtol(line, &end, 10);
        if (end != line && *end == '\0' && errno != ERANGE &&
            value >= min && value <= max) {
            *out = (int)value;
            return 1;
        }
        print("Enter an integer from", min, "to", max);
    }
    return 0;
}

int read_grade(const char *prompt, double *out) {
    char line[80];
    while (read_line(prompt, line, sizeof(line))) {
        char *end;
        errno = 0;
        double value = strtod(line, &end);
        if (end != line && *end == '\0' && errno != ERANGE &&
            value >= 0.0 && value <= 100.0) {
            *out = value;
            return 1;
        }
        print("Enter a grade from 0 to 100.");
    }
    return 0;
}

class StudentDB {
    Student rows[MAX_STUDENTS];
    size_t count;

    StudentDB(void) {
        this->count = 0;
    }

    int find(int id) {
        for (auto i = 0; i < this->count; ++i)
            if (this->rows[i].id == id) return (int)i;
        return -1;
    }

    void list(void) {
        print("\nID       NAME                            AGE    GRADE");
        print("----------------------------------------------------");
        for (auto i = 0; i < this->count; ++i) {
            Student *s = &this->rows[i];
            printf("%-8d %-31s %-6d %5.1f\n",
                   s->id, s->name, s->age, s->grade);
        }
        print("Total students:", this->count);
    }

    void show(void) {
        int id;
        if (!read_int("Student ID: ", 1, INT_MAX, &id)) return;
        int index = this->find(id);
        if (index < 0) { print("Student not found."); return; }
        Student *s = &this->rows[index];
        print("\nStudent ID:", s->id);
        print("Name:", s->name);
        print("Age:", s->age);
        print("Grade:", s->grade);
    }

    void add(void) {
        if (this->count >= MAX_STUDENTS) {
            print("Database is full.");
            return;
        }
        Student s = {0};
        if (!read_int("Student ID: ", 1, INT_MAX, &s.id)) return;
        if (this->find(s.id) >= 0) {
            print("That ID already exists.");
            return;
        }
        if (!read_line("Name: ", s.name, sizeof(s.name))) return;
        if (!s.name[0] || strchr(s.name, '|')) {
            print("Name must not be empty or contain '|'.");
            return;
        }
        if (!read_int("Age (1-120): ", 1, 120, &s.age)) return;
        if (!read_grade("Grade (0-100): ", &s.grade)) return;
        this->rows[this->count++] = s;
        print("Student created.");
    }

    void update(void) {
        int id;
        if (!read_int("Student ID to update: ", 1, INT_MAX, &id)) return;
        int index = this->find(id);
        if (index < 0) { print("Student not found."); return; }

        Student revised = this->rows[index];
        if (!read_line("New name: ", revised.name, sizeof(revised.name))) return;
        if (!revised.name[0] || strchr(revised.name, '|')) {
            print("Invalid name; record unchanged.");
            return;
        }
        if (!read_int("New age (1-120): ", 1, 120, &revised.age)) return;
        if (!read_grade("New grade (0-100): ", &revised.grade)) return;
        this->rows[index] = revised;
        print("Student updated.");
    }

    void remove(int id) {
        int index = this->find(id);
        if (index < 0) { print("Student not found."); return; }
        for (auto i = (size_t)index; i + 1 < this->count; ++i)
            this->rows[i] = this->rows[i + 1];
        --this->count;
        print("Student deleted.");
    }

    void load(void) {
        FILE *file = fopen(DATA_FILE, "r");
        if (!file) return;
        char line[256];
        while (this->count < MAX_STUDENTS && fgets(line, sizeof(line), file)) {
            Student s = {0};
            /* Names cannot contain '|'; records are one line each. */
            if (sscanf(line, "%d|%63[^|]|%d|%lf", &s.id, s.name,
                       &s.age, &s.grade) != 4) continue;
            if (s.id < 1 || s.age < 1 || s.age > 120 ||
                !(s.grade >= 0.0 && s.grade <= 100.0) ||
                this->find(s.id) >= 0) continue;
            this->rows[this->count++] = s;
        }
        if (fclose(file) != 0) perror("Closing database");
    }

    int save(void) {
        FILE *file = fopen(DATA_FILE ".tmp", "w");
        if (!file) { perror("Saving database"); return 0; }
        for (auto i = 0; i < this->count; ++i) {
            Student *s = &this->rows[i];
            if (fprintf(file, "%d|%s|%d|%.17g\n",
                        s->id, s->name, s->age, s->grade) < 0) {
                print("Database write failed.");
                fclose(file);
                return 0;
            }
        }
        if (fclose(file) != 0) { perror("Closing database"); return 0; }
        if (rename(DATA_FILE ".tmp", DATA_FILE) != 0) {
            perror("Replacing database");
            return 0;
        }
        return 1;
    }
};

int main(void) {
    StudentDB db = StudentDB();
    db.load();

    for (;;) {
        print("\n========== STUDENT RECORDS | C+ ==========");
        print("1. Add student");
        print("2. List students");
        print("3. Find student");
        print("4. Update student");
        print("5. Delete student");
        print("0. Save and exit");

        int choice;
        if (!read_int("Choose: ", 0, 5, &choice)) break;
        if (choice == 0) break;
        if (choice == 1) db.add();
        if (choice == 2) db.list();
        if (choice == 3) db.show();
        if (choice == 4) db.update();
        if (choice == 5) {
            int id;
            if (read_int("Student ID to delete: ", 1, INT_MAX, &id))
                db.remove(id);
        }
        if (choice == 1 || choice == 4 || choice == 5)
            if (!db.save()) print("Warning: changes not saved.");
    }
    if (!db.save()) return 1;
    print("Records saved. Goodbye!");
    return 0;
}

