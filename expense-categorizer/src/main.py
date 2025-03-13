import argparse

from transformers import pipeline, logging as hf_logging
from src.datalayer import connect_to_db, get_categories

hf_logging.set_verbosity_error()


def infer_category(description):
    """Infer category for a given description using zero-shot classification."""

    # Connect to the database and get categories
    conn = connect_to_db()
    if not conn:
        return None

    categories = get_categories(conn)
    conn.close()

    # Prepare candidate labels
    candidate_labels = [f"{label}, {desc}" for label, desc in categories]

    # Initialize the zero-shot classification pipeline
    classifier = pipeline("zero-shot-classification", model="facebook/bart-large-mnli")

    # Perform classification
    result = classifier(description, candidate_labels)

    # Format the result
    formatted_result = {
        "labels": [
            {"label": label.split(",")[0], "id": idx + 1}
            for idx, label in enumerate(result["labels"])
        ],
        "scores": result["scores"],
        "sequence": description,
    }

    return formatted_result


def main():
    parser = argparse.ArgumentParser(description="Expense Categorizer")
    parser.add_argument("command", help="Command to run")
    parser.add_argument("--description", help="Description of the item")

    args = parser.parse_args()

    if args.command == "category-infer" and args.description:
        category_id = infer_category(args.description)
        print(f"{category_id}")
    else:
        print("Invalid command or missing description.")


if __name__ == "__main__":
    main()
