#!/bin/bash
set -e

echo "📡 Creating IoT Rules for Wildfire Sensor System..."

REGION="us-east-2"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RULES_DIR="${SCRIPT_DIR}/../../iot-rules"

# Get AWS Account ID
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
echo "AWS Account ID: $ACCOUNT_ID"

# Get role ARNs from CloudFormation exports
IOT_TIMESTREAM_ROLE_ARN=$(aws cloudformation list-exports --region $REGION --query "Exports[?Name=='IoTTimestreamRoleArn'].Value" --output text)
IOT_S3_ROLE_ARN=$(aws cloudformation list-exports --region $REGION --query "Exports[?Name=='IoTS3RoleArn'].Value" --output text)
IOT_LAMBDA_ROLE_ARN=$(aws cloudformation list-exports --region $REGION --query "Exports[?Name=='IoTLambdaRoleArn'].Value" --output text)

# Get S3 bucket name from CloudFormation exports
S3_BUCKET_NAME=$(aws cloudformation list-exports --region $REGION --query "Exports[?Name=='WildfireSensorArchiveBucketName'].Value" --output text)

echo "Role ARNs retrieved:"
echo "  - IoT Timestream Role: $IOT_TIMESTREAM_ROLE_ARN"
echo "  - IoT S3 Role: $IOT_S3_ROLE_ARN"
echo "  - IoT Lambda Role: $IOT_LAMBDA_ROLE_ARN"
echo "  - S3 Bucket: $S3_BUCKET_NAME"

# Function to check if rule exists
rule_exists() {
    aws iot get-topic-rule --rule-name "$1" --region $REGION &>/dev/null
    return $?
}

# Function to delete rule if exists
delete_rule_if_exists() {
    if rule_exists "$1"; then
        echo "  Rule $1 already exists, deleting..."
        aws iot delete-topic-rule --rule-name "$1" --region $REGION
        echo "  Deleted existing rule $1"
    fi
}

# Rule 1: Sensor to Timestream
echo ""
echo "Creating Rule 1: SensorToTimestream..."
delete_rule_if_exists "SensorToTimestream"

# Replace placeholders in rule file
RULE1_TEMP=$(mktemp)
sed "s|REPLACE_WITH_IOT_TIMESTREAM_ROLE_ARN|${IOT_TIMESTREAM_ROLE_ARN}|g" "${RULES_DIR}/rule-to-timestream.json" > "$RULE1_TEMP"

aws iot create-topic-rule \
    --region $REGION \
    --cli-input-json file://"$RULE1_TEMP"

rm "$RULE1_TEMP"
echo "✅ Created SensorToTimestream rule"

# Rule 2: Sensor to Lambda Enricher
echo ""
echo "Creating Rule 2: SensorToEnricher..."
echo "⚠️  Note: This rule requires Lambda function 'data-enricher' to be deployed first"
echo "    Skipping for now. You can create this rule manually after deploying Lambda functions."

# Rule 3: Sensor to S3 Archive
echo ""
echo "Creating Rule 3: SensorToS3Archive..."
delete_rule_if_exists "SensorToS3Archive"

# Replace placeholders in rule file
RULE3_TEMP=$(mktemp)
sed -e "s|REPLACE_WITH_S3_BUCKET_NAME|${S3_BUCKET_NAME}|g" \
    -e "s|REPLACE_WITH_IOT_S3_ROLE_ARN|${IOT_S3_ROLE_ARN}|g" \
    "${RULES_DIR}/rule-to-s3-archive.json" > "$RULE3_TEMP"

aws iot create-topic-rule \
    --region $REGION \
    --cli-input-json file://"$RULE3_TEMP"

rm "$RULE3_TEMP"
echo "✅ Created SensorToS3Archive rule"

echo ""
echo "✅ IoT Rules creation complete!"
echo ""
echo "Active Rules:"
aws iot list-topic-rules --region $REGION --query 'rules[?starts_with(ruleName, `Sensor`)].{Name:ruleName,Created:createdAt}' --output table
